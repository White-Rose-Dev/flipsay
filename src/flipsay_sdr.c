#include "flipsay.h"

#include <furi_hal_usb_cdc.h>
#include "usb_cdc.h"
#include "cli/cli_vcp.h"

#include <string.h>

/* FlipSay <-> PC bridge protocol (USB CDC channel 1):
 *   'F' 'S' type len payload... xor     (xor of type, len and payload bytes)
 * PC -> Flipper: 0x10 HELLO, 0x11 TUNE(center_khz u32, span_khz u32), 0x12 GAIN(u8), 0x13 PING, 0x14 BYE
 * Flipper -> PC: 0x01 HELLO_ACK(proto, radio, bins), 0x02 FRAME(center u32, span u32, bins, seq, ms u16, levels...)
 */

#define CDC_IF 1

enum {
    TxAck = 0x01,
    TxFrame = 0x02,
    RxHello = 0x10,
    RxTune = 0x11,
    RxGain = 0x12,
    RxPing = 0x13,
    RxBye = 0x14,
};

typedef struct {
    FsApp* app;
    volatile bool usb_up;
    volatile bool rx_flag;
    volatile bool tx_done;
    bool hello;
    uint8_t seq;
    uint32_t last_rx;
    uint32_t fps_t;
    uint16_t fps_n;
    uint8_t ps;
    uint8_t ptype;
    uint8_t plen;
    uint8_t pidx;
    uint8_t pxor;
    uint8_t pbuf[16];
    uint8_t tx[160];
} Sdr;

static Sdr* g_sdr = NULL;

static void on_tx(void* ctx) {
    Sdr* s = ctx;
    s->tx_done = true;
}

static void on_rx(void* ctx) {
    Sdr* s = ctx;
    s->rx_flag = true;
}

static void on_state(void* ctx, CdcState st) {
    Sdr* s = ctx;
    s->usb_up = (st == CdcStateConnected);
}

static void on_ctrl(void* ctx, CdcCtrlLine lines) {
    UNUSED(ctx);
    UNUSED(lines);
}

static void on_cfg(void* ctx, struct usb_cdc_line_coding* cfg) {
    UNUSED(ctx);
    UNUSED(cfg);
}

static const CdcCallbacks sdr_cb = {on_tx, on_rx, on_state, on_ctrl, on_cfg};

static bool cdc_write(Sdr* s, const uint8_t* data, size_t len) {
    size_t off = 0;
    while(off < len) {
        if(!s->usb_up) return false;
        size_t n = len - off > 64 ? 64 : len - off;
        s->tx_done = false;
        furi_hal_cdc_send(CDC_IF, (uint8_t*)data + off, (uint16_t)n);
        uint32_t t0 = furi_get_tick();
        while(!s->tx_done) {
            if(furi_get_tick() - t0 > 60) return false;
            furi_delay_tick(1);
        }
        off += n;
    }
    return true;
}

static bool send_msg(Sdr* s, uint8_t type, const uint8_t* payload, uint8_t len) {
    uint8_t* f = s->tx;
    f[0] = 'F';
    f[1] = 'S';
    f[2] = type;
    f[3] = len;
    uint8_t x = (uint8_t)(type ^ len);
    for(uint8_t i = 0; i < len; i++) {
        f[4 + i] = payload[i];
        x ^= payload[i];
    }
    f[4 + len] = x;
    return cdc_write(s, f, (size_t)len + 5);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void handle_msg(Sdr* s) {
    FsApp* app = s->app;
    switch(s->ptype) {
    case RxHello: {
        uint8_t ack[3] = {1, app->radio, FS_BINS};
        s->hello = true;
        send_msg(s, TxAck, ack, 3);
        break;
    }
    case RxTune:
        if(s->plen >= 8) {
            uint32_t c = rd32(s->pbuf), sp = rd32(s->pbuf + 4);
            if(c >= 100000 && c <= 1000000) app->sdr_center_khz = c;
            if(sp >= 100 && sp <= 12000) app->sdr_span_khz = sp;
        }
        break;
    case RxGain:
        if(s->plen >= 1) app->sdr_gain = s->pbuf[0];
        break;
    case RxBye:
        s->hello = false;
        break;
    default:
        break;
    }
}

static void feed(Sdr* s, uint8_t b) {
    switch(s->ps) {
    case 0:
        if(b == 'F') s->ps = 1;
        break;
    case 1:
        s->ps = (b == 'S') ? 2 : (b == 'F' ? 1 : 0);
        break;
    case 2:
        s->ptype = b;
        s->pxor = b;
        s->ps = 3;
        break;
    case 3:
        s->plen = b;
        s->pxor ^= b;
        s->pidx = 0;
        if(b > sizeof(s->pbuf))
            s->ps = 0;
        else
            s->ps = b ? 4 : 5;
        break;
    case 4:
        s->pbuf[s->pidx++] = b;
        s->pxor ^= b;
        if(s->pidx >= s->plen) s->ps = 5;
        break;
    default:
        if(b == s->pxor) {
            s->last_rx = furi_get_tick();
            handle_msg(s);
        }
        s->ps = 0;
        break;
    }
}

void fs_sdr_start(FsApp* app) {
    if(g_sdr) return;
    Sdr* s = malloc(sizeof(Sdr));
    memset(s, 0, sizeof(Sdr));
    s->app = app;
    g_sdr = s;
    app->sdr_state = 0;
    app->sdr_frames = 0;
    app->sdr_fps10 = 0;
    app->sdr_gain = 255;
    if(!app->sdr_center_khz) app->sdr_center_khz = 433920;
    if(!app->sdr_span_khz) app->sdr_span_khz = 2400;

    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_cdc_dual, NULL) == true);
    CliVcp* cli = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_enable(cli);
    furi_record_close(RECORD_CLI_VCP);
    furi_hal_cdc_set_callbacks(CDC_IF, (CdcCallbacks*)&sdr_cb, s);
}

void fs_sdr_stop(FsApp* app) {
    Sdr* s = g_sdr;
    if(!s) return;
    furi_hal_cdc_set_callbacks(CDC_IF, NULL, NULL);
    CliVcp* cli = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_disable(cli);
    furi_record_close(RECORD_CLI_VCP);
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_cdc_single, NULL) == true);
    cli = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_enable(cli);
    furi_record_close(RECORD_CLI_VCP);
    g_sdr = NULL;
    free(s);
    app->sdr_state = 0;
}

void fs_sdr_poll(FsApp* app) {
    Sdr* s = g_sdr;
    if(!s) return;
    if(s->rx_flag) {
        s->rx_flag = false;
        uint8_t b[64];
        int32_t n = furi_hal_cdc_receive(CDC_IF, b, sizeof(b));
        for(int32_t i = 0; i < n; i++) feed(s, b[i]);
    }
    uint32_t now = furi_get_tick();
    bool dtr = (furi_hal_cdc_get_ctrl_line_state(CDC_IF) & CdcCtrlLineDTR) != 0;
    if(!s->usb_up) {
        s->hello = false;
        app->sdr_state = 0;
    } else if(!dtr || !s->hello) {
        s->hello = s->hello && dtr;
        app->sdr_state = 1;
    } else if(now - s->last_rx > 4000) {
        s->hello = false;
        app->sdr_state = 1;
    } else {
        app->sdr_state = 2;
    }
}

void fs_sdr_send_frame(
    FsApp* app,
    const uint8_t* lv,
    uint8_t n,
    uint32_t center,
    uint32_t span,
    uint16_t sweep_ms) {
    Sdr* s = g_sdr;
    if(!s || app->sdr_state != 2) return;
    uint8_t pl[12 + FS_BINS];
    if(n > FS_BINS) n = FS_BINS;
    for(int i = 0; i < 4; i++) {
        pl[i] = (uint8_t)(center >> (8 * i));
        pl[4 + i] = (uint8_t)(span >> (8 * i));
    }
    pl[8] = n;
    pl[9] = s->seq++;
    pl[10] = (uint8_t)sweep_ms;
    pl[11] = (uint8_t)(sweep_ms >> 8);
    memcpy(pl + 12, lv, n);
    if(send_msg(s, TxFrame, pl, (uint8_t)(12 + n))) {
        app->sdr_frames++;
        s->fps_n++;
    }
    uint32_t now = furi_get_tick();
    if(now - s->fps_t >= 1000) {
        app->sdr_fps10 = (uint16_t)((uint32_t)s->fps_n * 10000UL / (now - s->fps_t));
        s->fps_n = 0;
        s->fps_t = now;
    }
}
