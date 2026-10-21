#!/usr/bin/env python3
"""FlipSay SDR bridge: Flipper Zero (FlipSay SDR Mode) -> rtl_tcp server for GQRX.

The Flipper's CC1101 has no IQ output. FlipSay sweeps the band (RSSI) and sends the
spectrum over USB. This script turns that spectrum into a *synthetic* IQ stream and
serves it with the rtl_tcp protocol, so GQRX (or any rtl_tcp client) shows the spectrum
and waterfall, and its frequency / sample rate / gain controls retune the Flipper.

There is no real demodulation: the IQ data is generated from the measured spectrum
(random phase), so it carries no audio or bit information. See README.

Usage:
    python flipsay_sdr_bridge.py                 # auto-detect the Flipper port
    python flipsay_sdr_bridge.py --port COM7
    python flipsay_sdr_bridge.py --demo          # no hardware, fake spectrum
    python flipsay_sdr_bridge.py --selftest      # offline check of the rtl_tcp side
"""

import argparse
import socket
import struct
import sys
import threading
import time

import numpy as np

VERSION = "0.2.1"

# ---- Flipper link protocol (see src/flipsay_sdr.c) ----
RX_ACK, RX_FRAME = 0x01, 0x02
TX_HELLO, TX_TUNE, TX_GAIN, TX_PING, TX_BYE = 0x10, 0x11, 0x12, 0x13, 0x14

# ---- rtl_tcp ----
CMD_FREQ, CMD_RATE, CMD_GAINMODE, CMD_GAIN = 0x01, 0x02, 0x03, 0x04
CMD_IFGAIN, CMD_AGC, CMD_GAIN_IDX = 0x06, 0x08, 0x0D
R820T_GAINS = [0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254,
               280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496]

FLIPPER_MIN_KHZ, FLIPPER_MAX_KHZ = 281000, 962000
SPAN_MIN_KHZ, SPAN_MAX_KHZ = 200, 12000


def lvl_to_dbm(lvl):
    return -120.0 + np.asarray(lvl, dtype=np.float64) * 100.0 / 255.0


class Tune:
    """Shared tuning state, written by the rtl_tcp client, read by everyone."""

    def __init__(self):
        self.lock = threading.Lock()
        self.freq_hz = 433_920_000
        self.rate_hz = 2_400_000
        self.gain_pct = 255  # 255 = auto

    def center_span_khz(self):
        with self.lock:
            c = int(round(self.freq_hz / 1000.0))
            s = int(round(self.rate_hz / 1000.0))
        c = min(max(c, 100000), 1000000)
        s = min(max(s, SPAN_MIN_KHZ), SPAN_MAX_KHZ)
        return c, s


def frame_bytes(ftype, payload=b""):
    body = bytes([ftype, len(payload)]) + payload
    x = 0
    for b in body:
        x ^= b
    return b"FS" + body + bytes([x])


class FrameParser:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data):
        self.buf += data
        out = []
        while True:
            i = self.buf.find(b"FS")
            if i < 0:
                del self.buf[:-1]
                break
            if i > 0:
                del self.buf[:i]
            if len(self.buf) < 5:
                break
            ftype, ln = self.buf[2], self.buf[3]
            if len(self.buf) < 5 + ln:
                break
            payload = bytes(self.buf[4:4 + ln])
            x = ftype ^ ln
            for b in payload:
                x ^= b
            if x == self.buf[4 + ln]:
                out.append((ftype, payload))
                del self.buf[:5 + ln]
            else:
                del self.buf[:2]
        return out


class BaseLink:
    """Common interface: latest() -> (center_khz, span_khz, levels[128], seq) or None."""

    def __init__(self, tune):
        self.tune = tune
        self.lock = threading.Lock()
        self._latest = None
        self._seq = 0
        self.connected = False
        self.frames = 0

    def _store(self, c, s, lv):
        with self.lock:
            self._seq += 1
            self._latest = (c, s, lv)
            self.frames += 1

    def latest(self):
        with self.lock:
            if self._latest is None:
                return None
            c, s, lv = self._latest
            return c, s, lv, self._seq


class FlipperLink(BaseLink, threading.Thread):
    def __init__(self, tune, port=None):
        BaseLink.__init__(self, tune)
        threading.Thread.__init__(self, daemon=True)
        self.port = port
        self.ser = None
        self.parser = FrameParser()
        self.want_gain = None
        self.stop_flag = False

    @staticmethod
    def candidates():
        from serial.tools import list_ports
        ports = list(list_ports.comports())

        def score(p):
            s = 0
            if p.vid == 0x0483:
                s -= 2
            if "flipper" in (p.description or "").lower() or "flip" in (p.manufacturer or "").lower():
                s -= 1
            return s

        ports.sort(key=score)
        return [p.device for p in ports]

    def _open(self, device):
        import serial
        ser = serial.serial_for_url(device, 115200, timeout=0.05, write_timeout=1)
        ser.dtr = True
        ser.rts = False
        return ser

    def _probe(self, device):
        try:
            ser = self._open(device)
        except Exception:
            return None
        try:
            p = FrameParser()
            t0 = time.time()
            last = 0
            while time.time() - t0 < 1.2:
                if time.time() - last > 0.4:
                    ser.write(frame_bytes(TX_HELLO))
                    last = time.time()
                for ft, _ in p.feed(ser.read(256)):
                    if ft == RX_ACK:
                        return ser
        except Exception:
            pass
        ser.close()
        return None

    def _connect(self):
        while not self.stop_flag:
            devs = [self.port] if self.port else self.candidates()
            for d in devs:
                ser = self._probe(d)
                if ser:
                    print(f"[flipper] connected on {d}")
                    self.ser = ser
                    self.connected = True
                    return True
            print("[flipper] no FlipSay SDR port found. Open Options > SDR MODE on the Flipper "
                  "(waiting...)")
            time.sleep(2.0)
        return False

    def run(self):
        while not self.stop_flag:
            if not self._connect():
                return
            sent = None
            t_ping = t_tune = last_rx = time.time()
            try:
                while not self.stop_flag:
                    data = self.ser.read(512)
                    now = time.time()
                    if data:
                        last_rx = now
                    for ft, pl in self.parser.feed(data):
                        if ft == RX_FRAME and len(pl) >= 12:
                            c, s, n, seq, ms = struct.unpack("<IIBBH", pl[:12])
                            lv = np.frombuffer(pl[12:12 + n], dtype=np.uint8).copy()
                            if len(lv) == n and n > 1:
                                self._store(c, s, lv)
                    want = self.tune.center_span_khz()
                    if want != sent or now - t_tune > 2.0:
                        self.ser.write(frame_bytes(TX_TUNE, struct.pack("<II", *want)))
                        sent = want
                        t_tune = now
                        g = self.tune.gain_pct
                        self.ser.write(frame_bytes(TX_GAIN, bytes([g & 0xFF])))
                    if now - t_ping > 1.0:
                        self.ser.write(frame_bytes(TX_PING))
                        t_ping = now
                    if now - last_rx > 5.0:
                        print("[flipper] link silent, reconnecting")
                        break
            except Exception as e:
                print(f"[flipper] link error: {e}")
            self.connected = False
            try:
                self.ser.close()
            except Exception:
                pass
            time.sleep(1.0)

    def close(self):
        self.stop_flag = True
        try:
            if self.ser:
                self.ser.write(frame_bytes(TX_BYE))
        except Exception:
            pass


class DemoLink(BaseLink, threading.Thread):
    """Fake Flipper: a noise floor plus a few fixed 'transmitters'."""

    SIGNALS = [(433920, 30, -50), (315000, 25, -68), (868350, 40, -60), (915000, 50, -72),
               (433420, 20, -80), (344500, 20, -66)]

    def __init__(self, tune):
        BaseLink.__init__(self, tune)
        threading.Thread.__init__(self, daemon=True)
        self.rng = np.random.default_rng(1)
        self.connected = True

    def run(self):
        t0 = time.time()
        while True:
            c, s = self.tune.center_span_khz()
            xs = c + np.linspace(-s / 2.0, s / 2.0, 128)
            dbm = -100.0 + self.rng.normal(0, 2.0, 128)
            wob = 3.0 * np.sin((time.time() - t0) * 2.0)
            for f, w, p in self.SIGNALS:
                dbm = np.maximum(dbm, p + wob - ((xs - f) / w) ** 2 * 3.0)
            lv = np.clip((dbm + 120.0) * 255.0 / 100.0, 0, 255).astype(np.uint8)
            self._store(c, s, lv)
            time.sleep(0.12)


class Synth:
    """Spectrum -> pseudo IQ (random phase IFFT, sqrt-Hann overlap-add)."""

    N = 4096
    HOP = 2048
    SCALE = 50.0

    def __init__(self, link, tune):
        self.link = link
        self.tune = tune
        n = self.N
        self.win = np.sin(np.pi * (np.arange(n) + 0.5) / n)
        self.tail = np.zeros(self.HOP, dtype=np.complex128)
        self.rng = np.random.default_rng()
        self.key = None
        self.amp = np.full(n, 1e-3)
        self.k = np.arange(n) - n // 2

    def _profile(self, freq_hz, rate_hz):
        fr = self.link.latest()
        key = (None if fr is None else fr[3], freq_hz, rate_hz)
        if key == self.key:
            return
        self.key = key
        f = freq_hz + self.k * (rate_hz / self.N)
        if fr is None:
            dbm = np.full(self.N, -105.0)
        else:
            c, s, lv, _ = fr
            lo = c * 1000.0 - s * 500.0
            x = (f - lo) / (s * 1000.0) * (len(lv) - 1)
            level = np.interp(x, np.arange(len(lv)), lv.astype(np.float64), left=0.0, right=0.0)
            dbm = lvl_to_dbm(level)
        a_db = np.clip(-40.0 + (dbm + 105.0) * 0.6, -60.0, 0.0)
        self.amp = np.fft.ifftshift(10.0 ** (a_db / 20.0))

    def block(self, freq_hz, rate_hz):
        self._profile(freq_hz, rate_hz)
        n = self.N
        z = (self.rng.standard_normal(n) + 1j * self.rng.standard_normal(n)) * (self.amp / np.sqrt(2.0))
        x = np.fft.ifft(z) * np.sqrt(n) * self.win
        out = self.tail + x[:self.HOP]
        self.tail = x[self.HOP:]
        iq = np.empty(2 * self.HOP, dtype=np.float64)
        iq[0::2] = out.real
        iq[1::2] = out.imag
        iq = iq * self.SCALE + 127.5 + (self.rng.random(iq.size) - 0.5)
        return np.clip(np.round(iq), 0, 255).astype(np.uint8).tobytes()


class RtlServer:
    def __init__(self, host, port, link, tune):
        self.link, self.tune = link, tune
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.stop_flag = False

    def _commands(self, conn, alive):
        try:
            while alive[0]:
                buf = b""
                while len(buf) < 5:
                    chunk = conn.recv(5 - len(buf))
                    if not chunk:
                        alive[0] = False
                        return
                    buf += chunk
                cmd, val = struct.unpack(">BI", buf)
                self._apply(cmd, val)
        except OSError:
            alive[0] = False

    def _apply(self, cmd, val):
        t = self.tune
        with t.lock:
            if cmd == CMD_FREQ:
                t.freq_hz = val
                print(f"[gqrx] frequency {val / 1e6:.4f} MHz")
            elif cmd == CMD_RATE:
                t.rate_hz = max(val, 1000)
                print(f"[gqrx] sample rate {val / 1e6:.3f} MS/s -> span {val / 1e3:.0f} kHz")
            elif cmd == CMD_GAIN:
                t.gain_pct = int(min(max(val / 496.0 * 100.0, 0), 100))
                print(f"[gqrx] gain {val / 10.0:.1f} dB -> {t.gain_pct}%")
            elif cmd == CMD_GAIN_IDX:
                idx = min(val, len(R820T_GAINS) - 1)
                t.gain_pct = int(R820T_GAINS[idx] / 496.0 * 100.0)
            elif cmd in (CMD_GAINMODE, CMD_AGC):
                if val == 0 and cmd == CMD_GAINMODE:
                    t.gain_pct = 255
                elif cmd == CMD_AGC and val:
                    t.gain_pct = 255

    def serve_forever(self):
        print(f"[rtl_tcp] listening on port {self.port}")
        while not self.stop_flag:
            try:
                conn, addr = self.sock.accept()
            except OSError:
                return
            print(f"[rtl_tcp] client {addr[0]}:{addr[1]}")
            self._client(conn)
            print("[rtl_tcp] client gone")

    def _client(self, conn):
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        conn.sendall(b"RTL0" + struct.pack(">II", 5, len(R820T_GAINS)))
        alive = [True]
        threading.Thread(target=self._commands, args=(conn, alive), daemon=True).start()
        synth = Synth(self.link, self.tune)
        nxt = time.perf_counter()
        try:
            while alive[0] and not self.stop_flag:
                with self.tune.lock:
                    f, r = self.tune.freq_hz, self.tune.rate_hz
                conn.sendall(synth.block(f, r))
                nxt += Synth.HOP / float(r)
                d = nxt - time.perf_counter()
                if d > 0:
                    time.sleep(d)
                elif d < -0.5:
                    nxt = time.perf_counter()
        except OSError:
            pass
        finally:
            alive[0] = False
            try:
                conn.close()
            except OSError:
                pass


def selftest():
    tune = Tune()
    link = DemoLink(tune)
    link.start()
    time.sleep(0.4)
    server = RtlServer("127.0.0.1", 0, link, tune)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    cli = socket.create_connection(("127.0.0.1", server.port), timeout=5)
    hdr = b""
    while len(hdr) < 12:
        hdr += cli.recv(12 - len(hdr))
    assert hdr[:4] == b"RTL0", hdr
    tuner, gains = struct.unpack(">II", hdr[4:])
    print(f"header ok: tuner={tuner} gains={gains}")

    def capture(freq, rate, blocks=96):
        cli.sendall(struct.pack(">BI", CMD_RATE, rate))
        cli.sendall(struct.pack(">BI", CMD_FREQ, freq))
        cli.sendall(struct.pack(">BI", CMD_GAIN, 200))
        time.sleep(0.6)
        cli.settimeout(10)
        need = blocks * Synth.N * 2
        data = b""
        while len(data) < need + 400000:
            data += cli.recv(65536)
        raw = np.frombuffer(data[-need:], dtype=np.uint8).astype(np.float32) - 127.5
        iq = raw[0::2] + 1j * raw[1::2]
        spec = np.zeros(Synth.N)
        w = np.hanning(Synth.N)
        for b in range(blocks):
            seg = iq[b * Synth.N:(b + 1) * Synth.N]
            spec += np.abs(np.fft.fftshift(np.fft.fft(seg * w))) ** 2
        return 10 * np.log10(spec / blocks + 1e-9)

    ok = True
    for freq, rate, expect_off in ((433_920_000, 2_400_000, 0.0),
                                   (433_500_000, 2_400_000, 420e3),
                                   (433_700_000, 1_000_000, 220e3)):
        spec = capture(freq, rate)
        k = np.argmax(np.convolve(spec, np.ones(5) / 5, mode="same"))
        off = (k - Synth.N // 2) * rate / Synth.N
        floor = float(np.median(spec))
        peak = float(spec[k])
        good = abs(off - expect_off) < 40e3 and peak - floor > 12
        ok &= good
        print(f"freq {freq / 1e6:.3f} rate {rate / 1e6:.1f}: peak offset {off / 1e3:+.0f} kHz "
              f"(expected {expect_off / 1e3:+.0f}), peak-floor {peak - floor:.1f} dB -> "
              f"{'OK' if good else 'FAIL'}")
    assert tune.gain_pct == int(200 / 496.0 * 100), tune.gain_pct
    print("gain command ok:", tune.gain_pct, "%")
    print("SELFTEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="FlipSay SDR mode -> rtl_tcp bridge for GQRX")
    ap.add_argument("--port", help="Flipper serial port (default: auto-detect)")
    ap.add_argument("--listen", default="127.0.0.1", help="rtl_tcp bind address")
    ap.add_argument("--rtl-port", type=int, default=1234, help="rtl_tcp TCP port")
    ap.add_argument("--demo", action="store_true", help="fake spectrum, no Flipper needed")
    ap.add_argument("--selftest", action="store_true", help="offline test of the rtl_tcp server")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(selftest())
    if args.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(p.device, p.vid, p.description)
        return

    tune = Tune()
    link = DemoLink(tune) if args.demo else FlipperLink(tune, args.port)
    link.start()
    server = RtlServer(args.listen, args.rtl_port, link, tune)
    print(f"FlipSay SDR bridge {VERSION}")
    print(f"GQRX device string:  rtl_tcp={args.listen}:{server.port}")
    print("Input rate 2400000 works well. Ctrl+C to quit.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.stop_flag = True
        if hasattr(link, "close"):
            link.close()


if __name__ == "__main__":
    main()
