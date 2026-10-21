#include "flipsay.h"

#include <momentum/momentum.h>
#include <expansion/expansion.h>
#include <lib/drivers/cc1101_regs.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/types.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <toolbox/stream/stream.h>

#include <stdio.h>
#include <string.h>

#define TAG "FlipSay"

#define RING_SZ 1024
#define NRF_CE  (&gpio_ext_pb2)

#define BOOT(p, m)                  \
    do {                            \
        app->boot_msg = (m);        \
        app->boot_pct = (uint8_t)(p); \
    } while(0)

static const uint16_t dwell_us_tab[4] = {300, 800, 1800, 4000};
static const uint8_t nrf_samples_tab[4] = {2, 4, 8, 16};
static const uint16_t bw_khz_tab[6] = {58, 102, 203, 325, 464, 812};
static const uint8_t bw_code_tab[6] = {0xF0, 0xC0, 0x80, 0x50, 0x30, 0x00};
static const uint16_t gap_us_tab[5] = {1000, 2000, 4000, 8000, 16000};

static const FuriHalSubGhzPreset mod_preset_tab[7] = {
    FuriHalSubGhzPresetOok650Async,
    FuriHalSubGhzPresetOok270Async,
    FuriHalSubGhzPreset2FSKDev238Async,
    FuriHalSubGhzPreset2FSKDev12KAsync,
    FuriHalSubGhzPreset2FSKDev476Async,
    FuriHalSubGhzPresetGFSK9_99KbAsync,
    FuriHalSubGhzPresetMSK99_97KbAsync,
};

static const char* const mod_name_tab[7] = {
    "FuriHalSubGhzPresetOok650Async",
    "FuriHalSubGhzPresetOok270Async",
    "FuriHalSubGhzPreset2FSKDev238Async",
    "FuriHalSubGhzPreset2FSKDev12KAsync",
    "FuriHalSubGhzPreset2FSKDev476Async",
    "FuriHalSubGhzPresetGFSK9_99KbAsync",
    "FuriHalSubGhzPresetMSK99_97KbAsync",
};

static const uint32_t dec_freqs[] = {
    300000, 303875, 304250, 307800, 315000, 318000, 390000, 418000, 433075,
    433420, 433920, 434420, 434775, 438900, 868350, 915000, 925000,
};
#define N_DEC_FREQS (sizeof(dec_freqs) / sizeof(dec_freqs[0]))

/* ---------- pulse ring (scope + capture) ---------- */

static volatile uint32_t ring_head = 0;
static uint32_t* ring_buf = NULL;

static void pulse_cb(bool level, uint32_t duration, void* ctx) {
    UNUSED(ctx);
    uint32_t* rb = ring_buf;
    if(!rb) return;
    uint32_t h = ring_head;
    rb[h & (RING_SZ - 1)] = (level ? 0x80000000UL : 0UL) | (duration & 0x7FFFFFFFUL);
    ring_head = h + 1;
}

static void ring_acquire(void) {
    if(!ring_buf && memmgr_get_free_heap() > 10 * 1024) ring_buf = malloc(RING_SZ * sizeof(uint32_t));
    ring_head = 0;
}

static void ring_release(void) {
    uint32_t* p = ring_buf;
    ring_buf = NULL;
    if(p) free(p);
}

/* ---------- decoder context ---------- */

typedef struct {
    SubGhzEnvironment* env;
    SubGhzReceiver* rx;
    SubGhzProtocolRegistry* reg;
    const SubGhzProtocol** items;
    FlipperFormat* ff;
    FuriString* text;
    SubGhzRadioPreset preset;
    char proto[24];
    volatile bool got;
    uint8_t hash[4];
    uint32_t htick[4];
    uint8_t hn;
    bool have;
    bool lv;
    uint32_t ld;
} Dec;

typedef struct {
    uint8_t addr[5];
    uint8_t ch;
    uint8_t cnt;
    uint8_t plen;
    uint8_t pay[7];
    uint32_t last;
} Cand;

typedef struct {
    FsApp* app;
    FsRadio kind;
    const SubGhzDevice* dev;
    const SubGhzDevice* dev_int;
    const SubGhzDevice* dev_ext;
    const FuriHalSpiBusHandle* nh;
    bool pg;
    bool int_open;
    bool ext_open;
    bool nrf_open;
    bool nrf_held;
    bool otg_by_us;
    bool async_on;
    int hw_mode;
    uint32_t hw_rev;
    uint32_t hw_lock;
    int hw_bw;
    int hw_bw1;
    uint8_t hw_rate;
    int hw_lock_ch;
    int cur_mode;
    int raw_mode;
    int prev_mode;
    uint32_t ring_tail;
    uint8_t meas[3][FS_BINS];
    uint8_t meas_ok[3][FS_BINS];
    FsPulse* burst;
    uint16_t burst_n;
    File* logf;
    uint32_t log_t0;
    uint32_t meter_t;
    uint32_t peak_t;
    uint8_t meter_acc;
    uint32_t rssi_t;
    uint32_t gate_until;
    uint32_t last_yield;
    FsGeo last_geo[3];
    bool have_geo[3];
    char scratch[600];
    Dec* dec;
    uint32_t dec_cool;
    uint8_t nrf_mode;
    uint8_t nrf_toggle;
    struct WifiIo* wio;
    bool sdr_on;
    uint8_t sdr_gain_seen;
    uint8_t pkt_ch;
    uint32_t pkt_win_t;
    uint16_t pkt_win_count;
    Cand cand[10];
} Ctx;

static bool g_nrf_hold = false;

void fs_worker_post(FsApp* app, const char* msg) {
    snprintf(app->worker_msg, sizeof(app->worker_msg), "%s", msg);
    app->worker_msg_flag = true;
}

/* ---------- SPI / raw probes ---------- */

static const FuriHalSpiBusHandle* cc_handle(void) {
    return momentum_settings.spi_cc1101_handle == SpiDefault ?
               &furi_hal_spi_bus_handle_external :
               &furi_hal_spi_bus_handle_external_extra;
}

static const FuriHalSpiBusHandle* nrf_handle(void) {
    return momentum_settings.spi_nrf24_handle == SpiDefault ?
               &furi_hal_spi_bus_handle_external :
               &furi_hal_spi_bus_handle_external_extra;
}

static const GpioPin* other_cs(const FuriHalSpiBusHandle* h) {
    return (h == &furi_hal_spi_bus_handle_external) ? &gpio_ext_pc3 : &gpio_ext_pa4;
}

static void spi_init(const FuriHalSpiBusHandle* h) {
    furi_hal_gpio_init_simple(other_cs(h), GpioModeOutputPushPull);
    furi_hal_gpio_write(other_cs(h), true);
    furi_hal_spi_bus_handle_init(h);
}

static void spi_deinit(const FuriHalSpiBusHandle* h) {
    furi_hal_spi_bus_handle_deinit(h);
    furi_hal_gpio_init_simple(other_cs(h), GpioModeAnalog);
}

static uint8_t cc_raw_status(const FuriHalSpiBusHandle* h, uint8_t hdr) {
    uint8_t tx[2] = {hdr, 0};
    uint8_t rx[2] = {0, 0};
    furi_hal_spi_acquire(h);
    furi_hal_gpio_write(h->cs, false);
    uint32_t t0 = furi_get_tick();
    while(furi_hal_gpio_read(h->miso) && (furi_get_tick() - t0) < 5) {
    }
    furi_hal_spi_bus_trx(h, tx, rx, 2, 10);
    furi_hal_gpio_write(h->cs, true);
    furi_hal_spi_release(h);
    furi_delay_us(50);
    return rx[1];
}

static bool probe_cc1101(const FuriHalSpiBusHandle* h) {
    uint8_t part = cc_raw_status(h, 0xF0);
    uint8_t ver = cc_raw_status(h, 0xF1);
    return part == 0x00 && ver != 0x00 && ver != 0xFF && ver < 0x30;
}

static void nrf_trx(const FuriHalSpiBusHandle* h, uint8_t* tx, uint8_t* rx, uint8_t n) {
    if(!g_nrf_hold) furi_hal_spi_acquire(h);
    furi_hal_gpio_write(h->cs, false);
    furi_hal_spi_bus_trx(h, tx, rx, n, 500);
    furi_hal_gpio_write(h->cs, true);
    if(!g_nrf_hold) furi_hal_spi_release(h);
}

static uint8_t nrf_w(const FuriHalSpiBusHandle* h, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = {(uint8_t)(0x20 | (reg & 0x1F)), val};
    uint8_t rx[2] = {0, 0};
    nrf_trx(h, tx, rx, 2);
    return rx[0];
}

static uint8_t nrf_r(const FuriHalSpiBusHandle* h, uint8_t reg) {
    uint8_t tx[2] = {(uint8_t)(reg & 0x1F), 0xFF};
    uint8_t rx[2] = {0, 0};
    nrf_trx(h, tx, rx, 2);
    return rx[1];
}

static void nrf_wbuf(const FuriHalSpiBusHandle* h, uint8_t reg, const uint8_t* buf, uint8_t n) {
    uint8_t tx[8], rx[8];
    tx[0] = (uint8_t)(0x20 | (reg & 0x1F));
    memcpy(&tx[1], buf, n);
    nrf_trx(h, tx, rx, (uint8_t)(n + 1));
}

static uint8_t nrf_status(const FuriHalSpiBusHandle* h) {
    uint8_t tx[1] = {0xFF};
    uint8_t rx[1] = {0};
    nrf_trx(h, tx, rx, 1);
    return rx[0];
}

static void nrf_flush_rx(const FuriHalSpiBusHandle* h) {
    uint8_t tx[1] = {0xE2};
    uint8_t rx[1] = {0};
    nrf_trx(h, tx, rx, 1);
}

static bool probe_nrf(const FuriHalSpiBusHandle* h) {
    nrf_w(h, 0x05, 0x2A);
    uint8_t a = nrf_r(h, 0x05);
    nrf_w(h, 0x05, 0x55);
    uint8_t b = nrf_r(h, 0x05);
    nrf_w(h, 0x05, 0x00);
    return a == 0x2A && b == 0x55;
}

static void otg_on(Ctx* rc, const FsConfig* c) {
    if(!c->otg) return;
    if(!furi_hal_power_is_otg_enabled()) {
        uint8_t attempts = 0;
        while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
            furi_hal_power_enable_otg();
            furi_delay_ms(10);
        }
        rc->otg_by_us = furi_hal_power_is_otg_enabled();
        furi_delay_ms(80);
    }
}

static void nrf_configure(Ctx* rc, const FsConfig* c) {
    const FuriHalSpiBusHandle* h = rc->nh;
    nrf_w(h, 0x00, 0x03);
    furi_delay_ms(5);
    nrf_w(h, 0x01, 0x00);
    nrf_w(h, 0x02, 0x01);
    nrf_w(h, 0x06, c->nrf_rate ? 0x0F : 0x07);
    rc->hw_rate = c->nrf_rate;
    furi_hal_gpio_write(NRF_CE, false);
    rc->nrf_mode = 1;
}

static bool open_int(Ctx* rc) {
    rc->dev_int = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!rc->dev_int) return false;
    subghz_devices_reset(rc->dev_int);
    subghz_devices_idle(rc->dev_int);
    rc->int_open = true;
    return true;
}

static bool open_ext(Ctx* rc) {
    const SubGhzDevice* d = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(!d || !subghz_devices_is_connect(d) || !subghz_devices_begin(d)) return false;
    rc->dev_ext = d;
    subghz_devices_reset(d);
    subghz_devices_idle(d);
    rc->ext_open = true;
    return true;
}

static bool open_nrf(Ctx* rc, bool hold) {
    rc->nh = nrf_handle();
    spi_init(rc->nh);
    g_nrf_hold = false;
    if(!probe_nrf(rc->nh)) {
        spi_deinit(rc->nh);
        return false;
    }
    furi_hal_gpio_init(NRF_CE, GpioModeOutputPushPull, GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_write(NRF_CE, false);
    rc->nrf_open = true;
    if(hold) {
        furi_hal_spi_acquire(rc->nh);
        g_nrf_hold = true;
        rc->nrf_held = true;
    }
    return true;
}

static bool radio_open(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    rc->pg = app->pg_on;

    BOOT(56, "POWERING MODULES");
    if(c->source != FsSrcInt || rc->pg) otg_on(rc, c);

    bool is_cc = false, is_nrf = false;
    if(c->source == FsSrcAuto || rc->pg) {
        BOOT(62, "PROBING CC1101");
        const FuriHalSpiBusHandle* hc = cc_handle();
        spi_init(hc);
        is_cc = probe_cc1101(hc);
        spi_deinit(hc);
        if(!is_cc || nrf_handle() != hc) {
            BOOT(70, "PROBING NRF24");
            const FuriHalSpiBusHandle* hn = nrf_handle();
            spi_init(hn);
            g_nrf_hold = false;
            is_nrf = probe_nrf(hn);
            spi_deinit(hn);
        }
    }

    BOOT(80, "OPENING RADIO");
    if(rc->pg) {
        if(!open_int(rc)) {
            app->status = FsStatusFail;
            return false;
        }
        bool have_ext = false, have_nrf = false;
        if(is_cc) have_ext = open_ext(rc);
        if(is_nrf) have_nrf = open_nrf(rc, false);
        app->have_ext = have_ext;
        app->have_nrf = have_nrf;
        bool multi = c->pg_mode == 1;
        if(!multi && !have_ext) {
            app->status = FsStatusNoPg;
            return false;
        }
        app->multi_active = multi;
        rc->dev = have_ext ? rc->dev_ext : rc->dev_int;
        rc->kind = have_ext ? FsRadioExt : FsRadioInt;
        if(multi && memmgr_get_free_heap() < 12 * 1024) {
            app->status = FsStatusFail;
            fs_worker_post(app, "LOW MEMORY");
            return false;
        }
        app->radio = (uint8_t)rc->kind;
        app->status = FsStatusOk;
        return true;
    }

    app->have_ext = false;
    app->have_nrf = false;
    app->multi_active = false;
    FsRadio want = FsRadioNone;
    if(c->source == FsSrcInt) want = FsRadioInt;
    if(c->source == FsSrcExt) want = FsRadioExt;
    if(c->source == FsSrcNrf) want = FsRadioNrf;
    if(c->source == FsSrcAuto) want = is_cc ? FsRadioExt : (is_nrf ? FsRadioNrf : FsRadioInt);

    rc->kind = want;
    if(want == FsRadioInt) {
        if(!open_int(rc)) {
            app->status = FsStatusFail;
            return false;
        }
        rc->dev = rc->dev_int;
    } else if(want == FsRadioExt) {
        if(!open_ext(rc)) {
            app->status = FsStatusNoExt;
            return false;
        }
        rc->dev = rc->dev_ext;
    } else if(want == FsRadioNrf) {
        if(!open_nrf(rc, true)) {
            app->status = FsStatusNoNrf;
            return false;
        }
        nrf_configure(rc, c);
    }
    app->radio = (uint8_t)want;
    app->status = FsStatusOk;
    return true;
}

static void decode_free(Ctx* rc);

static void wifi_close(Ctx* rc);

static void radio_close(Ctx* rc) {
    wifi_close(rc);
    decode_free(rc);
    if(rc->async_on && rc->dev) {
        subghz_devices_stop_async_rx(rc->dev);
        rc->async_on = false;
    }
    ring_release();
    if(rc->int_open && rc->dev_int) {
        subghz_devices_idle(rc->dev_int);
        subghz_devices_sleep(rc->dev_int);
        rc->int_open = false;
    }
    if(rc->ext_open && rc->dev_ext) {
        subghz_devices_idle(rc->dev_ext);
        subghz_devices_sleep(rc->dev_ext);
        subghz_devices_end(rc->dev_ext);
        rc->ext_open = false;
    }
    if(rc->nrf_open) {
        furi_hal_gpio_write(NRF_CE, false);
        if(rc->nrf_held) {
            furi_hal_spi_release(rc->nh);
            rc->nrf_held = false;
        }
        g_nrf_hold = false;
        nrf_w(rc->nh, 0x00, 0x00);
        furi_hal_gpio_init(NRF_CE, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        spi_deinit(rc->nh);
        rc->nrf_open = false;
    }
    if(rc->otg_by_us && furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
}

/* ---------- helpers ---------- */

/* Sleep ~1 tick every few ms of radio work so the low-priority timer thread (needed by the
 * input service for button events) can run, without ever preempting a CC1101 busy-wait. */
static inline void yield_if_needed(Ctx* rc) {
    uint32_t t = furi_get_tick();
    if(t - rc->last_yield >= 4) {
        furi_delay_tick(1);
        rc->last_yield = furi_get_tick();
    }
}

static bool aborted(Ctx* rc) {
    FsApp* a = rc->app;
    return !a->worker_run || a->worker_mode != (uint8_t)rc->raw_mode || a->hw_rev != rc->hw_rev;
}

static int bw_index(const FsConfig* c, const FsGeo* g) {
    int idx = 5;
    if(c->bw >= 1 && c->bw <= 6) {
        idx = c->bw - 1;
    } else {
        uint32_t step = g->span_khz / (FS_BINS - 1);
        for(int i = 0; i < 6; i++) {
            if(bw_khz_tab[i] >= step) {
                idx = i;
                break;
            }
        }
    }
    if(c->auto_decode && idx < 2) idx = 2;
    return idx;
}

static void build_rssi_preset(uint8_t* dst, const FsConfig* c, int bw_idx) {
    const uint8_t* src = subghz_device_cc1101_preset_ook_650khz_async_regs;
    size_t i = 0, o = 0;
    bool has_mcsm0 = false;
    while(src[i] && o < 70) {
        uint8_t r = src[i], v = src[i + 1];
        if(r == CC1101_IOCFG0) {
            v = 0x2E;
        } else if(r == CC1101_MDMCFG4) {
            v = (uint8_t)(bw_code_tab[bw_idx] | (v & 0x0F));
        } else if(r == CC1101_MCSM0) {
            v = 0x08;
            has_mcsm0 = true;
        } else if(r == CC1101_AGCCTRL2) {
            uint8_t tgt = (bw_khz_tab[bw_idx] <= 102) ? 0x03 : 0x07;
            if(c->gain_tgt) tgt = (uint8_t)(c->gain_tgt - 1);
            uint8_t lna = c->gain_lna ? (uint8_t)(c->gain_lna - 1) : (uint8_t)((v >> 3) & 7);
            uint8_t dv = c->gain_dvga ? (uint8_t)(c->gain_dvga - 1) : (uint8_t)((v >> 6) & 3);
            v = (uint8_t)((dv << 6) | (lna << 3) | tgt);
        }
        dst[o++] = r;
        dst[o++] = v;
        i += 2;
    }
    if(!has_mcsm0) {
        dst[o++] = CC1101_MCSM0;
        dst[o++] = 0x08;
    }
    dst[o++] = 0;
    dst[o++] = 0;
    memcpy(&dst[o], &src[i + 2], 8);
}

static void load_sweep_preset(const SubGhzDevice* dev, const FsConfig* c, int bw_idx) {
    uint8_t buf[96];
    build_rssi_preset(buf, c, bw_idx);
    subghz_devices_idle(dev);
    subghz_devices_load_preset(dev, FuriHalSubGhzPresetCustom, buf);
}

static int nrf_ch_of(uint32_t khz) {
    if(khz + 500 < 2400000UL) return -1;
    uint32_t ch = (khz + 500 - 2400000UL) / 1000;
    if(ch > 127) return -1;
    return (int)ch;
}

static inline bool nrf_sample(Ctx* rc) {
    furi_hal_gpio_write(NRF_CE, true);
    furi_delay_us(130);
    furi_hal_gpio_write(NRF_CE, false);
    return (nrf_r(rc->nh, 0x09) & 1) != 0;
}

static void meter_update(Ctx* rc, uint8_t lvl) {
    FsApp* app = rc->app;
    uint32_t now = furi_get_tick();
    int32_t fx = app->meter_fx;
    fx += (((int32_t)lvl << 8) - fx) >> 1;
    app->meter_fx = (uint16_t)fx;
    app->meter_cur = (uint8_t)(fx >> 8);
    if(app->meter_cur >= app->meter_peak) {
        app->meter_peak = app->meter_cur;
        rc->peak_t = now;
    } else if(now - rc->peak_t > 70) {
        app->meter_peak--;
        rc->peak_t = now;
    }
    if(lvl > rc->meter_acc) rc->meter_acc = lvl;
    if(now - rc->meter_t >= 20) {
        app->meter_hist[app->meter_head] = rc->meter_acc;
        app->meter_head = (uint8_t)((app->meter_head + 1) % FS_HIST);
        rc->meter_acc = 0;
        rc->meter_t = now;
    }
}

/* ---------- hardware prepare ---------- */

static void cc_ensure(Ctx* rc, const FsConfig* c, int mode, const FsGeo* g0, const FsGeo* g1) {
    FsApp* app = rc->app;
    const bool two = rc->pg && rc->dev_int && rc->dev_int != rc->dev && rc->int_open;
    const bool multi = two && app->multi_active;
    int bw0 = bw_index(c, g0);
    int bw1 = multi ? bw_index(c, g1) : bw0;
    uint32_t lock = c->lock_khz[0];
    bool changed = (rc->hw_mode != mode) || (rc->hw_rev != app->hw_rev);
    if(mode == FsModeScope || mode == FsModeLock) {
        if(rc->hw_lock != lock) changed = true;
    }
    if(mode == FsModeSweep && (rc->hw_bw != bw0 || (two && rc->hw_bw1 != bw1))) changed = true;
    if(!changed) return;

    if(rc->async_on) {
        subghz_devices_stop_async_rx(rc->dev);
        rc->async_on = false;
        ring_release();
    }
    subghz_devices_idle(rc->dev);
    if(rc->dev_int && rc->dev_int != rc->dev && rc->int_open) subghz_devices_idle(rc->dev_int);

    if(mode != FsModeScope && rc->burst) {
        free(rc->burst);
        rc->burst = NULL;
    }
    if(mode == FsModeScope) {
        ring_acquire();
        if(!rc->burst && memmgr_get_free_heap() > 8 * 1024) rc->burst = malloc(FS_SCOPE_MAX * sizeof(FsPulse));
        subghz_devices_load_preset(rc->dev, mod_preset_tab[c->mod < 7 ? c->mod : 0], NULL);
        uint32_t f = lock * 1000UL;
        if(subghz_devices_is_frequency_valid(rc->dev, f)) {
            subghz_devices_set_frequency(rc->dev, f);
            subghz_devices_flush_rx(rc->dev);
            rc->ring_tail = ring_head;
            subghz_devices_start_async_rx(rc->dev, pulse_cb, NULL);
            rc->async_on = true;
        }
        rc->burst_n = 0;
    } else {
        load_sweep_preset(rc->dev, c, bw0);
        if(two && mode == FsModeSweep) load_sweep_preset(rc->dev_int, c, bw1);
        if(mode == FsModeLock) {
            uint32_t f = lock * 1000UL;
            if(subghz_devices_is_frequency_valid(rc->dev, f)) {
                subghz_devices_set_frequency(rc->dev, f);
                subghz_devices_set_rx(rc->dev);
            }
        }
    }
    rc->hw_mode = mode;
    rc->hw_rev = app->hw_rev;
    rc->hw_lock = lock;
    rc->hw_bw = bw0;
    rc->hw_bw1 = bw1;
}

#define HW_MULTI 100

static void cc_ensure_multi(Ctx* rc, const FsConfig* c, const FsGeo* gE, const FsGeo* gI) {
    FsApp* app = rc->app;
    bool changed = rc->hw_mode != HW_MULTI || rc->hw_rev != app->hw_rev;
    int bwE = gE ? bw_index(c, gE) : -1;
    int bwI = gI ? bw_index(c, gI) : -1;
    if(rc->hw_bw != bwE || rc->hw_bw1 != bwI) changed = true;
    if(!changed) return;

    if(rc->async_on && rc->dev) {
        subghz_devices_stop_async_rx(rc->dev);
        rc->async_on = false;
        ring_release();
    }
    if(rc->burst) {
        free(rc->burst);
        rc->burst = NULL;
    }
    if(rc->ext_open) subghz_devices_idle(rc->dev_ext);
    if(rc->int_open) subghz_devices_idle(rc->dev_int);
    if(gE && rc->ext_open) load_sweep_preset(rc->dev_ext, c, bwE);
    if(gI && rc->int_open) load_sweep_preset(rc->dev_int, c, bwI);
    rc->hw_mode = HW_MULTI;
    rc->hw_rev = app->hw_rev;
    rc->hw_bw = bwE;
    rc->hw_bw1 = bwI;
}

/* ---------- sweeps ---------- */

static int rssi_dbm(const SubGhzDevice* dev, int off) {
    return (int)subghz_devices_get_rssi(dev) + off;
}

static bool cc_sweep_dev(
    Ctx* rc,
    const FsConfig* c,
    const FsGeo* g,
    const SubGhzDevice* dev,
    int idx,
    int off) {
    const uint32_t step_hz = (g->span_khz * 1000UL) / (g->nmeas - 1);
    const uint32_t start_hz = g->start_khz * 1000UL;
    const uint32_t dwell = dwell_us_tab[c->speed & 3];
    for(int j = 0; j < g->nmeas; j++) {
        if((j & 7) == 0 && aborted(rc)) return false;
        yield_if_needed(rc);
        uint32_t f = start_hz + (uint32_t)j * step_hz;
        if(!subghz_devices_is_frequency_valid(dev, f)) {
            rc->meas[idx][j] = 0;
            rc->meas_ok[idx][j] = 0;
            continue;
        }
        subghz_devices_idle(dev);
        subghz_devices_set_frequency(dev, f);
        subghz_devices_set_rx(dev);
        furi_delay_us(dwell);
        rc->meas[idx][j] = fs_dbm_to_lvl(rssi_dbm(dev, off));
        rc->meas_ok[idx][j] = 1;
    }
    subghz_devices_idle(dev);
    return true;
}

static bool cc_sweep(Ctx* rc, const FsConfig* c, const FsGeo* g) {
    return cc_sweep_dev(rc, c, g, rc->dev, 0, c->rssi_off);
}

static bool cc_merged_sweep(Ctx* rc, const FsConfig* c, const FsGeo* g) {
    const uint32_t step_hz = (g->span_khz * 1000UL) / (g->nmeas - 1);
    const uint32_t start_hz = g->start_khz * 1000UL;
    const uint32_t dwell = dwell_us_tab[c->speed & 3];
    for(int j = 0; j < g->nmeas; j++) {
        if((j & 7) == 0 && aborted(rc)) return false;
        yield_if_needed(rc);
        uint32_t f = start_hz + (uint32_t)j * step_hz;
        bool ve = subghz_devices_is_frequency_valid(rc->dev_ext, f);
        bool vi = subghz_devices_is_frequency_valid(rc->dev_int, f);
        if(!ve && !vi) {
            rc->meas[0][j] = 0;
            rc->meas_ok[0][j] = 0;
            continue;
        }
        if(ve) {
            subghz_devices_idle(rc->dev_ext);
            subghz_devices_set_frequency(rc->dev_ext, f);
            subghz_devices_set_rx(rc->dev_ext);
        }
        if(vi) {
            subghz_devices_idle(rc->dev_int);
            subghz_devices_set_frequency(rc->dev_int, f);
            subghz_devices_set_rx(rc->dev_int);
        }
        furi_delay_us(dwell);
        int de = ve ? rssi_dbm(rc->dev_ext, c->off_ext) : -200;
        int di = vi ? rssi_dbm(rc->dev_int, c->off_int) : -200;
        int dbm;
        if(ve && vi)
            dbm = c->pg_combine ? (de + di) / 2 : (de > di ? de : di);
        else
            dbm = ve ? de : di;
        rc->meas[0][j] = fs_dbm_to_lvl(dbm + c->rssi_off);
        rc->meas_ok[0][j] = 1;
    }
    subghz_devices_idle(rc->dev_ext);
    subghz_devices_idle(rc->dev_int);
    return true;
}

static bool cc_multi_sweep(Ctx* rc, const FsConfig* c, const FsGeo* g0, const FsGeo* g1) {
    const uint32_t step0 = (g0->span_khz * 1000UL) / (g0->nmeas - 1);
    const uint32_t step1 = (g1->span_khz * 1000UL) / (g1->nmeas - 1);
    const uint32_t s0 = g0->start_khz * 1000UL;
    const uint32_t s1 = g1->start_khz * 1000UL;
    const uint32_t dwell = dwell_us_tab[c->speed & 3];
    for(int j = 0; j < g0->nmeas; j++) {
        if((j & 7) == 0 && aborted(rc)) return false;
        yield_if_needed(rc);
        uint32_t f0 = s0 + (uint32_t)j * step0;
        uint32_t f1 = s1 + (uint32_t)j * step1;
        bool v0 = subghz_devices_is_frequency_valid(rc->dev_ext, f0);
        bool v1 = subghz_devices_is_frequency_valid(rc->dev_int, f1);
        if(v0) {
            subghz_devices_idle(rc->dev_ext);
            subghz_devices_set_frequency(rc->dev_ext, f0);
            subghz_devices_set_rx(rc->dev_ext);
        }
        if(v1) {
            subghz_devices_idle(rc->dev_int);
            subghz_devices_set_frequency(rc->dev_int, f1);
            subghz_devices_set_rx(rc->dev_int);
        }
        furi_delay_us(dwell);
        rc->meas_ok[0][j] = v0;
        rc->meas[0][j] = v0 ? fs_dbm_to_lvl(rssi_dbm(rc->dev_ext, c->off_ext + c->rssi_off)) : 0;
        rc->meas_ok[1][j] = v1;
        rc->meas[1][j] = v1 ? fs_dbm_to_lvl(rssi_dbm(rc->dev_int, c->off_int + c->rssi_off)) : 0;
    }
    subghz_devices_idle(rc->dev_ext);
    subghz_devices_idle(rc->dev_int);
    return true;
}

static bool nrf_sweep(Ctx* rc, const FsConfig* c, const FsGeo* g, int idx) {
    const int samples = nrf_samples_tab[c->speed & 3];
    if(rc->hw_rate != c->nrf_rate) {
        nrf_w(rc->nh, 0x06, c->nrf_rate ? 0x0F : 0x07);
        rc->hw_rate = c->nrf_rate;
    }
    if(rc->nrf_mode != 1) nrf_configure(rc, c);
    int last_ch = -2;
    uint8_t last_val = 0, last_ok = 0;
    for(int j = 0; j < g->nmeas; j++) {
        if((j & 7) == 0 && aborted(rc)) return false;
        yield_if_needed(rc);
        uint32_t khz = g->start_khz + (g->span_khz * (uint32_t)j) / (g->nmeas - 1);
        int ch = nrf_ch_of(khz);
        if(ch < 0) {
            rc->meas[idx][j] = 0;
            rc->meas_ok[idx][j] = 0;
            continue;
        }
        if(ch != last_ch) {
            nrf_w(rc->nh, 0x05, (uint8_t)ch);
            int hits = 0;
            for(int s = 0; s < samples; s++) hits += nrf_sample(rc) ? 1 : 0;
            last_val = (uint8_t)(hits * 255 / samples);
            last_ok = 1;
            last_ch = ch;
        }
        rc->meas[idx][j] = last_val;
        rc->meas_ok[idx][j] = last_ok;
    }
    return true;
}

static void log_row(Ctx* rc);

static void publish_chan(Ctx* rc, int idx, const FsConfig* c, const FsGeo* g) {
    FsApp* app = rc->app;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    FsChan* ch = app->ch[idx];
    if(ch) {
        bool changed = !rc->have_geo[idx] || rc->last_geo[idx].start_khz != g->start_khz ||
                       rc->last_geo[idx].span_khz != g->span_khz ||
                       rc->last_geo[idx].nmeas != g->nmeas || rc->last_geo[idx].fam != g->fam;
        ch->geo = *g;
        if(changed) {
            fs_chan_reset(ch);
            rc->last_geo[idx] = *g;
            rc->have_geo[idx] = true;
        }
        for(int x = 0; x < FS_BINS; x++) {
            int j = (x * (g->nmeas - 1) + 63) / (FS_BINS - 1);
            ch->raw[x] = rc->meas[idx][j];
            ch->valid[x] = rc->meas_ok[idx][j];
        }
        if(!app->ui.freeze) fs_dsp_process(app, ch, c, idx == 0);
    }
    furi_mutex_release(app->mtx);
    if(idx == 0 && !app->boot_done) {
        BOOT(100, "READY");
        app->boot_done = true;
        fs_trace(app, "worker: first sweep published");
    } else if(idx == 0 && ch && (ch->sweeps % 40) == 0) {
        char tb[24];
        snprintf(tb, sizeof(tb), "sweep %lu", ch->sweeps);
        fs_trace(app, tb);
    }
    if(idx == 0 && rc->logf && !app->ui.freeze) log_row(rc);
}

/* ---------- scope ---------- */

static void scope_publish(Ctx* rc) {
    FsApp* app = rc->app;
    if(!rc->burst) return;
    uint16_t n = rc->burst_n;
    uint32_t total = 0, mn = 0xFFFFFFFFUL, mx = 0;
    for(uint16_t i = 0; i < n; i++) {
        uint32_t d = rc->burst[i].dur;
        total += d;
        if(d >= 30 && d < mn) mn = d;
        if(d > mx) mx = d;
    }
    if(mn == 0xFFFFFFFFUL) mn = 0;
    uint8_t hint = 0;
    if(mn) {
        int c1 = 0, c2 = 0, c3 = 0, co = 0;
        for(uint16_t i = 0; i < n; i++) {
            uint32_t r = (rc->burst[i].dur + mn / 2) / mn;
            if(r <= 1)
                c1++;
            else if(r == 2)
                c2++;
            else if(r == 3)
                c3++;
            else
                co++;
        }
        if(co * 4 > n)
            hint = 4;
        else if(c3 > 0 && c2 == 0)
            hint = 2;
        else if(c2 > 0 && c3 == 0)
            hint = 1;
        else if(c2 > 0 && c3 > 0)
            hint = 3;
    }
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    if(app->scope) memcpy(app->scope, rc->burst, n * sizeof(FsPulse));
    app->scope_n = app->scope ? n : 0;
    app->scope_total_us = total;
    app->scope_min_us = mn;
    app->scope_max_us = mx;
    app->scope_hint = hint;
    app->scope_bursts++;
    furi_mutex_release(app->mtx);
}

static void scope_pulse(Ctx* rc, bool lvl, uint32_t dur, uint32_t gap_us) {
    if(!rc->burst) return;
    if(dur >= gap_us) {
        if(rc->burst_n >= 8) scope_publish(rc);
        rc->burst_n = 0;
        return;
    }
    if(rc->burst_n >= FS_SCOPE_MAX) {
        scope_publish(rc);
        rc->burst_n = 0;
    }
    rc->burst[rc->burst_n].dur = (uint16_t)(dur > 65535 ? 65535 : dur);
    rc->burst[rc->burst_n].lvl = lvl ? 1 : 0;
    rc->burst_n++;
}

static void cc_scope_step(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    uint32_t now = furi_get_tick();
    uint32_t gap = gap_us_tab[c->scope_gap < 5 ? c->scope_gap : 1];

    if(now - rc->rssi_t >= 25 && rc->async_on) {
        rc->rssi_t = now;
        int dbm = rssi_dbm(rc->dev, c->rssi_off);
        app->rssi_dbm = (int16_t)dbm;
        uint8_t lvl = fs_dbm_to_lvl(dbm);
        meter_update(rc, lvl);
        uint8_t sq = fs_sq_level(c, 0);
        if(sq == 0 || lvl >= sq) rc->gate_until = now + 120;
    }
    bool gate_open = (fs_sq_level(c, 0) == 0) || (now < rc->gate_until);

    uint32_t* rb = ring_buf;
    if(rb) {
        uint32_t head = ring_head;
        if(head - rc->ring_tail > RING_SZ) rc->ring_tail = head - RING_SZ;
        while(rc->ring_tail != head) {
            uint32_t e = rb[rc->ring_tail & (RING_SZ - 1)];
            rc->ring_tail++;
            if(!gate_open) {
                rc->burst_n = 0;
                continue;
            }
            scope_pulse(rc, (e >> 31) != 0, e & 0x7FFFFFFFUL, gap);
        }
    }
    furi_delay_ms(4);
}

static void nrf_lock_step(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    int ch = nrf_ch_of(c->lock_khz[1]);
    if(ch < 0) {
        meter_update(rc, 0);
        furi_delay_ms(30);
        return;
    }
    if(rc->hw_lock_ch != ch) {
        nrf_w(rc->nh, 0x05, (uint8_t)ch);
        rc->hw_lock_ch = ch;
    }
    if(rc->hw_rate != c->nrf_rate) {
        nrf_w(rc->nh, 0x06, c->nrf_rate ? 0x0F : 0x07);
        rc->hw_rate = c->nrf_rate;
    }
    int total = 0;
    for(int col = 0; col < 4; col++) {
        int hits = 0;
        for(int s = 0; s < 4; s++) hits += nrf_sample(rc) ? 1 : 0;
        app->act[app->act_head] = (uint8_t)hits;
        app->act_head = (uint8_t)((app->act_head + 1) % FS_ACT_COLS);
        total += hits;
    }
    meter_update(rc, (uint8_t)(total * 255 / 16));
    furi_delay_ms(1);
}

static void cc_lock_step(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    uint32_t f = c->lock_khz[0] * 1000UL;
    if(!subghz_devices_is_frequency_valid(rc->dev, f)) {
        meter_update(rc, 0);
        furi_delay_ms(30);
        return;
    }
    int sum = 0;
    for(int k = 0; k < 4; k++) {
        furi_delay_us(400);
        sum += (int)subghz_devices_get_rssi(rc->dev);
    }
    int dbm = sum / 4 + c->rssi_off;
    app->rssi_dbm = (int16_t)dbm;
    meter_update(rc, fs_dbm_to_lvl(dbm));
    furi_delay_ms(1);
}

/* ---------- NRF24 packet monitor (Multi Use) ---------- */

static bool nrf_valid_addr(const uint8_t* a) {
    static const uint8_t bad[4][2] = {{0x55, 0x55}, {0xAA, 0xAA}, {0x00, 0x00}, {0xFF, 0xFF}};
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 2; j++)
            if(!memcmp(a + j * 2, bad[i], 2)) return false;
    return true;
}

static void nrf_promisc_init(Ctx* rc, const FsConfig* c) {
    const FuriHalSpiBusHandle* h = rc->nh;
    static const uint8_t preamble[2] = {0xAA, 0x00};
    static const uint8_t clear5[5] = {0, 0, 0, 0, 0};
    furi_hal_gpio_write(NRF_CE, false);
    nrf_w(h, 0x00, 0x00);
    nrf_w(h, 0x07, 0x70);
    nrf_w(h, 0x1C, 0x00);
    nrf_w(h, 0x01, 0x00);
    nrf_w(h, 0x1D, 0x05);
    nrf_w(h, 0x03, 0x00);
    nrf_w(h, 0x02, 0x01);
    nrf_wbuf(h, 0x0A, clear5, 5);
    nrf_wbuf(h, 0x0A, preamble, 2);
    nrf_w(h, 0x11, 32);
    nrf_flush_rx(h);
    nrf_w(h, 0x06, c->nrf_rate ? 0x0F : 0x07);
    nrf_w(h, 0x00, 0x03);
    furi_delay_ms(5);
    rc->nrf_mode = 2;
}

static void nrf_pkt_publish(Ctx* rc, const Cand* cd) {
    FsApp* app = rc->app;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    int slot = -1;
    for(int i = 0; i < app->n_pkts; i++) {
        if(app->pkts[i].ch == cd->ch && !memcmp(app->pkts[i].addr, cd->addr, 5)) {
            slot = i;
            break;
        }
    }
    if(slot < 0) {
        if(app->n_pkts < FS_MAX_PKTS) {
            slot = app->n_pkts++;
        } else {
            slot = 0;
            for(int i = 1; i < FS_MAX_PKTS; i++)
                if(app->pkts[i].last < app->pkts[slot].last) slot = i;
        }
        memset(&app->pkts[slot], 0, sizeof(FsPkt));
        memcpy(app->pkts[slot].addr, cd->addr, 5);
        app->pkts[slot].ch = cd->ch;
    }
    app->pkts[slot].count = (uint16_t)(app->pkts[slot].count + 1);
    app->pkts[slot].last = furi_get_tick();
    app->pkts[slot].plen = cd->plen;
    memcpy(app->pkts[slot].pay, cd->pay, 7);
    app->pkt_total++;
    rc->pkt_win_count++;
    furi_mutex_release(app->mtx);
}

static void nrf_pkt_slice(Ctx* rc, const FsConfig* c) {
    static const uint8_t dwell_tab[3] = {40, 25, 12};
    FsApp* app = rc->app;
    const FuriHalSpiBusHandle* h = rc->nh;
    int dwell = dwell_tab[c->mu_nrf_speed % 3];
    int hops = 120 / dwell;
    if(rc->nrf_mode != 2) {
        nrf_promisc_init(rc, c);
        rc->pkt_ch = 2;
    }
    furi_hal_gpio_write(NRF_CE, true);
    for(int k = 0; k < hops && app->worker_run; k++) {
        int ch = rc->pkt_ch;
        rc->pkt_ch = (uint8_t)(ch >= 80 ? 2 : ch + 1);
        nrf_w(h, 0x05, (uint8_t)ch);
        nrf_flush_rx(h);
        furi_delay_ms(dwell);
        uint8_t st = nrf_status(h);
        app->pkt_ch = (uint8_t)ch;
        if(st & 0x40) {
            uint8_t tx[33], rx[33];
            memset(tx, 0, sizeof(tx));
            tx[0] = 0x61;
            nrf_trx(h, tx, rx, 33);
            nrf_w(h, 0x07, 0x40);
            const uint8_t* p = &rx[1];
            if(nrf_valid_addr(p)) {
                uint32_t now = furi_get_tick();
                Cand* hit = NULL;
                for(int i = 0; i < 10; i++) {
                    if(rc->cand[i].cnt && rc->cand[i].ch == ch && !memcmp(rc->cand[i].addr, p, 5)) {
                        hit = &rc->cand[i];
                        break;
                    }
                }
                if(!hit) {
                    int oldest = 0;
                    for(int i = 1; i < 10; i++)
                        if(rc->cand[i].last < rc->cand[oldest].last) oldest = i;
                    hit = &rc->cand[oldest];
                    memset(hit, 0, sizeof(Cand));
                    memcpy(hit->addr, p, 5);
                    hit->ch = (uint8_t)ch;
                }
                if(hit->cnt < 250) hit->cnt++;
                hit->last = now;
                hit->plen = 7;
                memcpy(hit->pay, p + 5, 7);
                if(hit->cnt >= 2) nrf_pkt_publish(rc, hit);
            }
        }
    }
    furi_hal_gpio_write(NRF_CE, false);
    uint32_t now = furi_get_tick();
    if(now - rc->pkt_win_t >= 2000) {
        app->pkt_rate = (uint16_t)(rc->pkt_win_count * 10 / 2);
        rc->pkt_win_count = 0;
        rc->pkt_win_t = now;
    }
}

/* ---------- WiFi module (Marauder over UART) ---------- */

typedef struct WifiIo {
    FuriHalSerialHandle* ser;
    FuriStreamBuffer* rx;
    Expansion* exp;
    uint8_t st;
    uint8_t tries;
    uint8_t run_mode;
    uint8_t cfg_mode;
    bool seen_any;
    bool seen_mar;
    bool seen_cmds;
    uint32_t t;
    uint32_t t_mar;
    uint32_t sec_t;
    uint16_t sec_cnt;
    uint8_t ll;
    char line[100];
} WifiIo;

enum { WifiStClosed, WifiStBoot, WifiStInfo, WifiStHelp, WifiStRun, WifiStDead };

static const char* const wifi_cmds[4] = {"scanap", "sniffbeacon", "sniffdeauth", "sniffpmkid"};

static void wifi_rx_cb(FuriHalSerialHandle* h, FuriHalSerialRxEvent ev, void* ctx) {
    WifiIo* w = ctx;
    if(ev == FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(h);
        furi_stream_buffer_send(w->rx, &b, 1, 0);
    }
}

static void wifi_send(WifiIo* w, const char* cmd) {
    if(!w->ser) return;
    furi_hal_serial_tx(w->ser, (uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx(w->ser, (uint8_t*)"\n", 1);
}

static char lc(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
}

static const char* find_ci(const char* s, const char* n) {
    size_t nl = strlen(n);
    for(; *s; s++) {
        size_t i = 0;
        while(i < nl && s[i] && lc(s[i]) == n[i]) i++;
        if(i == nl) return s;
    }
    return NULL;
}

static bool read_int(const char* p, int* out) {
    while(*p == ' ') p++;
    bool neg = false;
    if(*p == '-') {
        neg = true;
        p++;
    }
    if(*p < '0' || *p > '9') return false;
    int v = 0;
    while(*p >= '0' && *p <= '9' && v < 100000) v = v * 10 + (*p++ - '0');
    *out = neg ? -v : v;
    return true;
}

static bool int_after(const char* s, const char* key, int* out) {
    const char* p = find_ci(s, key);
    return p && read_int(p + strlen(key), out);
}

static int hexv(char ch) {
    if(ch >= '0' && ch <= '9') return ch - '0';
    ch = lc(ch);
    if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static bool read_mac_tail(const char* p, uint8_t* tail) {
    while(*p == ' ') p++;
    uint8_t oct[6];
    for(int i = 0; i < 6; i++) {
        int a = hexv(p[0]), b = a < 0 ? -1 : hexv(p[1]);
        if(a < 0 || b < 0) return false;
        oct[i] = (uint8_t)(a * 16 + b);
        p += 2;
        if(i < 5) {
            if(*p != ':') return false;
            p++;
        }
    }
    memcpy(tail, &oct[3], 3);
    return true;
}

static void parse_ver(const char* p, char* out, size_t cap) {
    const char* v = find_ci(p, "version:");
    if(v) {
        v += 8;
        while(*v == ' ') v++;
    } else {
        for(v = p; *v; v++)
            if((*v == 'v' || *v == 'V') && v[1] >= '0' && v[1] <= '9') break;
        if(!*v) return;
    }
    size_t i = 0;
    while(*v && *v != ' ' && i < cap - 1) out[i++] = *v++;
    out[i] = 0;
}

static void copy_str(char* dst, size_t cap, const char* src) {
    size_t k = strlen(src);
    if(k > cap - 1) k = cap - 1;
    memcpy(dst, src, k);
    dst[k] = 0;
}

static void wifi_ap_update(
    FsWifi* s,
    const char* ssid,
    int rssi,
    int ch,
    const uint8_t* tail,
    bool has_tail) {
    int slot = -1;
    for(int i = 0; i < s->n_aps; i++) {
        bool same = has_tail ? !memcmp(s->aps[i].bssid_tail, tail, 3) :
                               (ssid[0] && !strcmp(s->aps[i].ssid, ssid));
        if(same) {
            slot = i;
            break;
        }
    }
    if(slot < 0) {
        if(s->n_aps < FS_MAX_APS) {
            slot = s->n_aps++;
        } else {
            slot = 0;
            for(int i = 1; i < FS_MAX_APS; i++)
                if(s->aps[i].last < s->aps[slot].last) slot = i;
        }
        memset(&s->aps[slot], 0, sizeof(FsAp));
        s->aps[slot].rssi = (int8_t)rssi;
    }
    FsAp* a = &s->aps[slot];
    if(rssi < -127) rssi = -127;
    if(rssi > 0) rssi = 0;
    a->rssi = (int8_t)((a->rssi * 2 + rssi) / 3);
    if(ch > 0 && ch < 200) a->ch = (uint8_t)ch;
    if(ssid[0]) copy_str(a->ssid, sizeof(a->ssid), ssid);
    if(has_tail) memcpy(a->bssid_tail, tail, 3);
    a->last = furi_get_tick();
}

static void wifi_line(WifiIo* w, FsWifi* s) {
    char* p = w->line;
    size_t n = strlen(p);
    while(n && (p[n - 1] == '\r' || p[n - 1] == ' ')) p[--n] = 0;
    while(*p == ' ' || *p == '#' || *p == '>') p++;
    if(!*p) return;
    w->seen_any = true;

    if(w->st == WifiStInfo) {
        if(find_ci(p, "marauder")) {
            if(!w->seen_mar) w->t_mar = furi_get_tick();
            w->seen_mar = true;
        }
        if(find_ci(p, "version:") || (find_ci(p, "marauder") && !s->ver[0]))
            parse_ver(p, s->ver, sizeof(s->ver));
        return;
    }
    if(w->st == WifiStHelp) {
        if(find_ci(p, "scanap") || find_ci(p, "sniffbeacon")) w->seen_cmds = true;
        return;
    }
    if(w->st != WifiStRun) return;

    for(int i = 0; i < 4; i++) {
        size_t l = strlen(wifi_cmds[i]);
        if(!strncmp(p, wifi_cmds[i], l)) return;
    }
    if(!strncmp(p, "stopscan", 8)) return;

    s->total++;
    w->sec_cnt++;
    copy_str(s->log[s->log_head], sizeof(s->log[0]), p);
    s->log_head = (uint8_t)((s->log_head + 1) % 4);

    int rssi = -100, ch = 0;
    bool has_rssi = int_after(p, "rssi:", &rssi);
    int_after(p, "ch:", &ch);
    char ssid[17];
    ssid[0] = 0;
    uint8_t tail[3] = {0, 0, 0};
    bool has_tail = false;
    const char* b = find_ci(p, "bssid:");
    if(b) has_tail = read_mac_tail(b + 6, tail);
    const char* e = find_ci(p, "essid:");
    if(e) {
        e += 6;
        while(*e == ' ') e++;
        copy_str(ssid, sizeof(ssid), e);
    }
    if(!has_rssi && p[0] == '[') {
        const char* c1 = find_ci(p, "[ch:");
        const char* br = c1 ? strchr(c1, ']') : NULL;
        if(c1 && br) {
            read_int(c1 + 4, &ch);
            const char* st = br + 1;
            while(*st == ' ') st++;
            const char* ls = strrchr(st, ' ');
            if(ls && ls > st && read_int(ls, &rssi)) {
                size_t k = (size_t)(ls - st);
                if(k > sizeof(ssid) - 1) k = sizeof(ssid) - 1;
                memcpy(ssid, st, k);
                ssid[k] = 0;
                has_rssi = true;
            }
        }
    }
    if(has_rssi && (has_tail || ssid[0])) wifi_ap_update(s, ssid, rssi, ch, tail, has_tail);
}

static void wifi_open(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    if(rc->wio) return;
    if(memmgr_get_free_heap() < 8 * 1024) return;
    FsWifi* s = malloc(sizeof(FsWifi));
    memset(s, 0, sizeof(FsWifi));
    s->state = FsWifiStarting;
    WifiIo* w = malloc(sizeof(WifiIo));
    memset(w, 0, sizeof(WifiIo));
    rc->wio = w;
    w->rx = furi_stream_buffer_alloc(2048, 1);
    w->exp = furi_record_open(RECORD_EXPANSION);
    expansion_disable(w->exp);
    w->ser = furi_hal_serial_control_acquire(momentum_settings.uart_esp_channel);
    if(!w->ser) {
        s->state = FsWifiBusy;
        w->st = WifiStDead;
    } else {
        otg_on(rc, c);
        furi_hal_serial_init(w->ser, 115200);
        furi_hal_serial_async_rx_start(w->ser, wifi_rx_cb, w, false);
        w->st = WifiStBoot;
        w->t = furi_get_tick();
        w->cfg_mode = c->wifi_mode;
    }
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    app->wifi = s;
    furi_mutex_release(app->mtx);
}

static void wifi_close(Ctx* rc) {
    FsApp* app = rc->app;
    WifiIo* w = rc->wio;
    if(!w) return;
    rc->wio = NULL;
    if(w->ser) {
        if(w->st == WifiStRun) {
            wifi_send(w, "stopscan");
            furi_delay_ms(60);
        }
        furi_hal_serial_async_rx_stop(w->ser);
        furi_hal_serial_deinit(w->ser);
        furi_hal_serial_control_release(w->ser);
    }
    expansion_enable(w->exp);
    furi_record_close(RECORD_EXPANSION);
    furi_stream_buffer_free(w->rx);
    free(w);
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    FsWifi* s = app->wifi;
    app->wifi = NULL;
    furi_mutex_release(app->mtx);
    if(s) free(s);
}

static void wifi_poll(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    WifiIo* w = rc->wio;
    if(!w || !w->ser) return;
    bool mode_change = false;
    uint32_t now = furi_get_tick();

    furi_mutex_acquire(app->mtx, FuriWaitForever);
    FsWifi* s = app->wifi;
    if(!s) {
        furi_mutex_release(app->mtx);
        return;
    }
    uint8_t buf[64];
    size_t got, budget = 0;
    while(budget < 1536 && (got = furi_stream_buffer_receive(w->rx, buf, sizeof(buf), 0)) > 0) {
        budget += got;
        for(size_t i = 0; i < got; i++) {
            char ch = (char)buf[i];
            if(ch == '\n') {
                w->line[w->ll] = 0;
                w->ll = 0;
                wifi_line(w, s);
            } else if(ch >= 32 && ch < 127 && w->ll < sizeof(w->line) - 1) {
                w->line[w->ll++] = ch;
            }
        }
    }

    switch(w->st) {
    case WifiStBoot:
        s->state = FsWifiStarting;
        if(now - w->t > 800) {
            while(furi_stream_buffer_receive(w->rx, buf, sizeof(buf), 0) > 0) {
            }
            w->ll = 0;
            wifi_send(w, "");
            wifi_send(w, "info");
            w->st = WifiStInfo;
            w->t = now;
        }
        break;
    case WifiStInfo:
        if(w->seen_mar && (s->ver[0] || now - w->t_mar > 500)) {
            s->state = FsWifiReady;
            w->st = WifiStRun;
            w->run_mode = c->wifi_mode;
            w->sec_t = now;
            wifi_send(w, wifi_cmds[c->wifi_mode & 3]);
        } else if(now - w->t > 1500 && !w->seen_mar) {
            if(!w->seen_any && w->tries < 2) {
                w->tries++;
                wifi_send(w, "");
                wifi_send(w, "info");
                w->t = now;
            } else if(!w->seen_any) {
                s->state = FsWifiNoResponse;
                w->st = WifiStDead;
            } else {
                wifi_send(w, "help");
                w->st = WifiStHelp;
                w->t = now;
            }
        }
        break;
    case WifiStHelp:
        if(w->seen_cmds) {
            if(!s->ver[0]) snprintf(s->ver, sizeof(s->ver), "?");
            s->state = FsWifiReady;
            w->st = WifiStRun;
            w->run_mode = c->wifi_mode;
            w->sec_t = now;
            wifi_send(w, wifi_cmds[c->wifi_mode & 3]);
        } else if(now - w->t > 1500) {
            s->state = FsWifiNotMarauder;
            w->st = WifiStDead;
        }
        break;
    case WifiStRun:
        if(now - w->sec_t >= 1000) {
            s->hist[s->hist_head] = (uint8_t)(w->sec_cnt > 255 ? 255 : w->sec_cnt);
            s->hist_head = (uint8_t)((s->hist_head + 1) % 32);
            s->rate = w->sec_cnt;
            w->sec_cnt = 0;
            w->sec_t = now;
        }
        if(w->run_mode != c->wifi_mode) mode_change = true;
        break;
    case WifiStDead:
        if(w->cfg_mode != c->wifi_mode) {
            w->cfg_mode = c->wifi_mode;
            w->st = WifiStBoot;
            w->t = now;
            w->tries = 0;
            w->seen_any = w->seen_mar = w->seen_cmds = false;
            s->ver[0] = 0;
            s->state = FsWifiStarting;
        }
        break;
    default:
        break;
    }
    furi_mutex_release(app->mtx);

    if(mode_change) {
        wifi_send(w, "stopscan");
        furi_delay_ms(150);
        w->run_mode = c->wifi_mode;
        w->cfg_mode = c->wifi_mode;
        wifi_send(w, wifi_cmds[c->wifi_mode & 3]);
    }
}
/* ---------- logging ---------- */

static void log_open(Ctx* rc, const FsConfig* c, const FsGeo* g) {
    FsApp* app = rc->app;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char* path = rc->scratch;
    snprintf(
        path,
        96,
        FS_DIR "/log_%04u%02u%02u_%02u%02u%02u.csv",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(f);
        fs_worker_post(app, "LOG: SD ERROR");
        return;
    }
    char* hdr = rc->scratch + 100;
    int n = snprintf(
        hdr,
        200,
        "# FlipSay %s CSV radio=%s start_khz=%lu span_khz=%lu bins=%d unit=%s step=%u\nms,peak_khz,peak_val,values...\n",
        FS_VERSION,
        rc->kind == FsRadioNrf ? "NRF24" : (rc->kind == FsRadioExt ? "EXT" : "INT"),
        g->start_khz,
        g->span_khz,
        FS_BINS,
        g->fam ? "pct" : "dbm",
        (unsigned)c->speed);
    storage_file_write(f, hdr, n);
    rc->logf = f;
    rc->log_t0 = furi_get_tick();
    app->log_rows = 0;
    app->log_active = true;
    fs_worker_post(app, "LOG STARTED");
}

static void log_close(Ctx* rc) {
    if(!rc->logf) return;
    storage_file_close(rc->logf);
    storage_file_free(rc->logf);
    rc->logf = NULL;
    rc->app->log_active = false;
}

static void log_row(Ctx* rc) {
    FsApp* app = rc->app;
    FsChan* ch = app->ch[0];
    if(!ch) return;
    char* row = rc->scratch;
    const int cap = (int)sizeof(rc->scratch) - 12;
    int len = 0;
    len += fs_itoa(row + len, (int32_t)(furi_get_tick() - rc->log_t0));
    row[len++] = ',';
    uint32_t pk = fs_col_khz(&ch->geo, ch->max_col);
    len += fs_itoa(row + len, (int32_t)pk);
    row[len++] = ',';
    int fam = ch->geo.fam;
    int pv = fam ? ch->trace_max * 100 / 255 : fs_lvl_to_dbm(ch->trace_max);
    len += fs_itoa(row + len, pv);
    for(int i = 0; i < FS_BINS && len < cap; i++) {
        row[len++] = ',';
        int v = ch->valid[i] ? (fam ? ch->raw[i] * 100 / 255 : fs_lvl_to_dbm(ch->raw[i])) : 0;
        len += fs_itoa(row + len, v);
    }
    row[len++] = '\n';
    storage_file_write(rc->logf, row, len);
    app->log_rows++;
}

/* ---------- capture to .sub ---------- */

static bool write_sub(Ctx* rc, uint32_t khz, uint8_t mod, int32_t* buf, int n) {
    FsApp* app = rc->app;
    char* path = rc->scratch;
    char* out = rc->scratch + 100;
    storage_simply_mkdir(app->storage, EXT_PATH("subghz"));
    snprintf(path, 96, EXT_PATH("subghz/FlipSay_%lu_%lu.sub"), khz, furi_get_tick() / 1000);
    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(f);
        return false;
    }
    int len = snprintf(
        out,
        260,
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu\nPreset: %s\nProtocol: RAW\n",
        khz * 1000UL,
        mod_name_tab[mod < 7 ? mod : 0]);
    storage_file_write(f, out, len);
    int i = 0;
    while(i < n) {
        len = snprintf(out, 260, "RAW_Data:");
        for(int k = 0; k < 100 && i < n; k++, i++) {
            out[len++] = ' ';
            len += fs_itoa(out + len, buf[i]);
            if(len > 230) {
                storage_file_write(f, out, len);
                len = 0;
            }
        }
        out[len++] = '\n';
        storage_file_write(f, out, len);
    }
    storage_file_close(f);
    storage_file_free(f);
    return true;
}

static void do_capture(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    app->cap_req = false;
    if(rc->kind == FsRadioNrf) {
        fs_worker_post(app, "CAPTURE: CC1101 ONLY");
        return;
    }
    uint32_t khz = app->cap_khz;
    uint32_t f = khz * 1000UL;
    if(!subghz_devices_is_frequency_valid(rc->dev, f)) {
        fs_worker_post(app, "CAPTURE: BAD FREQ");
        return;
    }
    if(memmgr_get_free_heap() < 22 * 1024) {
        fs_worker_post(app, "CAPTURE: LOW MEMORY");
        return;
    }
    int32_t* buf = malloc(FS_CAP_MAX * sizeof(int32_t));
    if(rc->async_on) {
        subghz_devices_stop_async_rx(rc->dev);
        rc->async_on = false;
    }
    ring_acquire();
    subghz_devices_idle(rc->dev);
    subghz_devices_load_preset(rc->dev, mod_preset_tab[c->mod < 7 ? c->mod : 0], NULL);
    subghz_devices_set_frequency(rc->dev, f);
    subghz_devices_flush_rx(rc->dev);
    rc->ring_tail = ring_head;
    subghz_devices_start_async_rx(rc->dev, pulse_cb, NULL);
    rc->async_on = true;
    rc->hw_mode = -1;
    fs_worker_post(app, "CAPTURING...");

    int gate_dbm = c->squelch ? (-105 + 5 * (c->squelch - 1)) : -85;
    uint32_t t0 = furi_get_tick();
    uint32_t last_act = 0;
    bool seen = false;
    int n = 0;
    while(app->worker_run && (furi_get_tick() - t0) < 6000 && n < FS_CAP_MAX) {
        furi_delay_ms(3);
        int dbm = rssi_dbm(rc->dev, c->rssi_off);
        uint32_t now = furi_get_tick();
        if(dbm >= gate_dbm) {
            last_act = now;
            seen = true;
        }
        bool recording = seen && (now - last_act) < 120;
        uint32_t head = ring_head;
        if(head - rc->ring_tail > RING_SZ) rc->ring_tail = head - RING_SZ;
        while(rc->ring_tail != head) {
            uint32_t e = ring_buf[rc->ring_tail & (RING_SZ - 1)];
            rc->ring_tail++;
            if(!recording) continue;
            bool lvl = (e >> 31) != 0;
            int32_t dur = (int32_t)(e & 0x7FFFFFFFUL);
            if(n == 0 && !lvl) continue;
            if(n < FS_CAP_MAX) buf[n++] = lvl ? dur : -dur;
        }
        if(seen && !recording && n >= 30 && (now - last_act) > 300) break;
    }
    subghz_devices_stop_async_rx(rc->dev);
    rc->async_on = false;
    subghz_devices_idle(rc->dev);
    ring_release();

    if(n >= 20) {
        if(write_sub(rc, khz, c->mod, buf, n)) {
            fs_worker_post(app, "SAVED TO /subghz");
            if(c->notify) fs_notify(app, 1, "Capture saved", "RAW .sub in /subghz");
        } else {
            fs_worker_post(app, "CAPTURE: SD ERROR");
        }
    } else {
        fs_worker_post(app, "NO SIGNAL CAPTURED");
    }
    free(buf);
}

/* ---------- auto decode ---------- */

static void dec_rx_cb(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* base, void* ctx) {
    UNUSED(receiver);
    Dec* d = ctx;
    if(d->got) return;
    const SubGhzProtocol* p = base->protocol;
    if(!(p->flag & SubGhzProtocolFlag_Save)) return;
    if(p->type == SubGhzProtocolTypeRAW || p->type == SubGhzProtocolTypeBinRAW ||
       p->type == SubGhzProtocolTypeUnknown)
        return;
    uint8_t h = subghz_protocol_decoder_base_get_hash_data(base);
    uint32_t now = furi_get_tick();
    for(int i = 0; i < 4; i++) {
        if(d->htick[i] && d->hash[i] == h && (now - d->htick[i]) < 15000) return;
    }
    FlipperFormat* ff = flipper_format_string_alloc();
    if(subghz_protocol_decoder_base_serialize(base, ff, &d->preset) != SubGhzProtocolStatusOk) {
        flipper_format_free(ff);
        return;
    }
    furi_string_reset(d->text);
    subghz_protocol_decoder_base_get_string(base, d->text);
    memset(d->proto, 0, sizeof(d->proto));
    strncpy(d->proto, p->name, sizeof(d->proto) - 1);
    d->hash[d->hn & 3] = h;
    d->htick[d->hn & 3] = now;
    d->hn++;
    d->ff = ff;
    d->got = true;
}

static bool decode_alloc(Ctx* rc) {
    if(memmgr_get_free_heap() < 36 * 1024) return false;
    Dec* d = malloc(sizeof(Dec));
    memset(d, 0, sizeof(Dec));

    size_t total = subghz_protocol_registry_count(&subghz_protocol_registry);
    d->items = malloc(total * sizeof(SubGhzProtocol*));
    size_t n = 0;
    const uint32_t ignore = SubGhzProtocolFilter_Weather | SubGhzProtocolFilter_Sensors |
                            SubGhzProtocolFilter_TPMS;
    for(size_t i = 0; i < total; i++) {
        const SubGhzProtocol* p = subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!p) continue;
        if(!(p->flag & SubGhzProtocolFlag_Save)) continue;
        if(p->type != SubGhzProtocolTypeStatic && p->type != SubGhzProtocolTypeDynamic) continue;
        if((uint32_t)p->filter & ignore) continue;
        d->items[n++] = p;
    }
    d->reg = malloc(sizeof(SubGhzProtocolRegistry));
    SubGhzProtocolRegistry tmp = {.items = d->items, .size = n};
    memcpy(d->reg, &tmp, sizeof(tmp));

    d->env = subghz_environment_alloc();
    subghz_environment_load_keystore(d->env, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(d->env, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(d->env, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(d->env, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_came_atomo_rainbow_table_file_name(d->env, SUBGHZ_CAME_ATOMO_DIR_NAME);
    subghz_environment_set_protocol_registry(d->env, d->reg);
    d->rx = subghz_receiver_alloc_init(d->env);
    subghz_receiver_set_filter(d->rx, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(d->rx, dec_rx_cb, d);
    d->text = furi_string_alloc();
    d->preset.name = furi_string_alloc();
    rc->dec = d;
    return true;
}

static void decode_free(Ctx* rc) {
    Dec* d = rc->dec;
    if(!d) return;
    rc->dec = NULL;
    if(d->rx) subghz_receiver_free(d->rx);
    if(d->env) subghz_environment_free(d->env);
    if(d->reg) free(d->reg);
    if(d->items) free(d->items);
    if(d->ff) flipper_format_free(d->ff);
    if(d->text) furi_string_free(d->text);
    if(d->preset.name) furi_string_free(d->preset.name);
    free(d);
}

static void dec_feed(Dec* d, bool level, uint32_t dur) {
    if(dur < 30) {
        d->ld += dur;
        return;
    }
    if(!d->have) {
        d->have = true;
        d->lv = level;
        d->ld = dur;
        return;
    }
    if(level == d->lv) {
        d->ld += dur;
        return;
    }
    subghz_receiver_decode(d->rx, d->lv, d->ld);
    d->lv = level;
    d->ld = dur;
}
static void split_lines(const char* s, char* l1, size_t n1, char* l2, size_t n2) {
    l1[0] = 0;
    l2[0] = 0;
    size_t i = 0, o = 0;
    while(s[i] && s[i] != '\n' && o < n1 - 1) l1[o++] = s[i++];
    l1[o] = 0;
    while(s[i] && s[i] != '\n') i++;
    if(s[i] == '\n') i++;
    o = 0;
    while(s[i] && s[i] != '\n' && o < n2 - 1) l2[o++] = s[i++];
    l2[o] = 0;
}

static void dec_handle_result(Ctx* rc, Dec* d, uint32_t khz) {
    FsApp* app = rc->app;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char safe[12];
    size_t k = 0;
    for(size_t i = 0; d->proto[i] && k < 10; i++) {
        char ch = d->proto[i];
        if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            safe[k++] = ch;
        else if(k > 0 && safe[k - 1] != '_')
            safe[k++] = '_';
    }
    safe[k] = 0;
    char* path = rc->scratch;
    snprintf(
        path,
        96,
        FS_SAVED_DIR "/%s_%02u%02u%02u%02u.sub",
        safe,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    storage_simply_mkdir(app->storage, FS_SAVED_DIR);
    flipper_format_rewind(d->ff);
    Stream* st = flipper_format_get_raw_stream(d->ff);
    stream_save_to_file(st, app->storage, path, FSOM_CREATE_ALWAYS);
    bool ok = storage_common_stat(app->storage, path, NULL) == FSE_OK;
    flipper_format_free(d->ff);
    d->ff = NULL;

    char l1[24], l2[40];
    split_lines(furi_string_get_cstr(d->text), l1, sizeof(l1), l2, sizeof(l2));
    if(!l1[0]) snprintf(l1, sizeof(l1), "%s", d->proto);
    if(!l2[0]) snprintf(l2, sizeof(l2), "%lu.%03lu MHz", khz / 1000, khz % 1000);

    if(ok) {
        app->dec_saved++;
        if(app->cfg.notify) {
            fs_notify(app, 0, l1, l2);
        } else {
            fs_worker_post(app, "DECODED + SAVED");
        }
    } else {
        fs_worker_post(app, "DECODE: SD ERROR");
    }
}

static bool decode_session(Ctx* rc, uint32_t khz, int thr_dbm) {
    static const FuriHalSubGhzPreset ps[4] = {
        FuriHalSubGhzPresetOok650Async,
        FuriHalSubGhzPresetOok270Async,
        FuriHalSubGhzPreset2FSKDev476Async,
        FuriHalSubGhzPreset2FSKDev238Async,
    };
    static const char* const pn[4] = {"AM650", "AM270", "FM476", "FM238"};
    FsApp* app = rc->app;
    Dec* d = rc->dec;
    bool done = false;
    app->dec_state = 2;
    app->dec_khz = khz;
    for(int k = 0; k < 4 && !done && app->worker_run; k++) {
        subghz_devices_idle(rc->dev);
        subghz_devices_load_preset(rc->dev, ps[k], NULL);
        subghz_devices_set_frequency(rc->dev, khz * 1000UL);
        subghz_devices_flush_rx(rc->dev);
        d->got = false;
        furi_string_set_str(d->preset.name, pn[k]);
        d->preset.frequency = khz * 1000UL;
        d->preset.latitude = 0;
        d->preset.longitude = 0;
        subghz_receiver_reset(d->rx);
        d->have = false;
        d->ld = 0;
        ring_acquire();
        rc->ring_tail = ring_head;
        subghz_devices_start_async_rx(rc->dev, pulse_cb, NULL);
        uint32_t t0 = furi_get_tick();
        uint32_t quiet = 0;
        while(app->worker_run && (furi_get_tick() - t0) < 1100 && !d->got) {
            furi_delay_ms(4);
            uint32_t* rb = ring_buf;
            uint32_t head = ring_head;
            if(rb) {
                if(head - rc->ring_tail > RING_SZ) {
                    rc->ring_tail = head - RING_SZ;
                    subghz_receiver_reset(d->rx);
                    d->have = false;
                }
                while(rc->ring_tail != head && !d->got) {
                    uint32_t e = rb[rc->ring_tail & (RING_SZ - 1)];
                    rc->ring_tail++;
                    dec_feed(d, (e >> 31) != 0, e & 0x7FFFFFFFUL);
                }
            }
            int dbm = (int)subghz_devices_get_rssi(rc->dev);
            uint32_t now = furi_get_tick();
            if(dbm < thr_dbm - 8) {
                if(!quiet) quiet = now;
            } else {
                quiet = 0;
            }
            if(quiet && (now - quiet) > 450 && (now - t0) > 500) break;
        }
        subghz_devices_stop_async_rx(rc->dev);
        subghz_devices_idle(rc->dev);
        ring_release();
        if(d->got) {
            dec_handle_result(rc, d, khz);
            done = true;
        }
    }
    rc->hw_mode = -1;
    app->dec_state = 0;
    return done;
}

static void decode_step(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    if(rc->kind == FsRadioNrf || !rc->dev) return;
    uint32_t now = furi_get_tick();
    if(now < rc->dec_cool) return;
    if(!rc->dec) {
        if(!decode_alloc(rc)) {
            furi_mutex_acquire(app->mtx, FuriWaitForever);
            app->cfg.auto_decode = 0;
            furi_mutex_release(app->mtx);
            fs_worker_post(app, "DECODE: LOW MEMORY");
            return;
        }
    }
    int thr = -90 + 10 * (c->dec_sens > 2 ? 1 : c->dec_sens);
    uint32_t cand[N_DEC_FREQS + 1];
    int nc = 0;
    for(size_t i = 0; i < N_DEC_FREQS; i++) cand[nc++] = dec_freqs[i];
    FsChan* ch0 = (app->multi_active && rc->dev == rc->dev_int) ? app->ch[1] : app->ch[0];
    if(ch0 && ch0->n_peaks > 0 && fs_lvl_to_dbm(ch0->peaks[0].lvl) >= thr) {
        uint32_t pk = fs_col_khz(&ch0->geo, ch0->peaks[0].col);
        bool near = false;
        for(size_t i = 0; i < N_DEC_FREQS; i++) {
            uint32_t df = pk > dec_freqs[i] ? pk - dec_freqs[i] : dec_freqs[i] - pk;
            if(df < 250) near = true;
        }
        if(!near) cand[nc++] = pk;
    }

    app->dec_state = 1;
    int best = -200;
    uint32_t best_khz = 0;
    for(int i = 0; i < nc && app->worker_run; i++) {
        uint32_t f = cand[i] * 1000UL;
        yield_if_needed(rc);
        if(!subghz_devices_is_frequency_valid(rc->dev, f)) continue;
        subghz_devices_idle(rc->dev);
        subghz_devices_set_frequency(rc->dev, f);
        subghz_devices_set_rx(rc->dev);
        furi_delay_us(600);
        int dbm = rssi_dbm(rc->dev, c->rssi_off);
        if(dbm > best) {
            best = dbm;
            best_khz = cand[i];
        }
    }
    app->dec_state = 0;
    if(best_khz == 0 || best < thr) {
        subghz_devices_idle(rc->dev);
        return;
    }
    int ok = 0;
    for(int k = 0; k < 4; k++) {
        furi_delay_us(800);
        if(rssi_dbm(rc->dev, c->rssi_off) >= thr - 3) ok++;
    }
    if(ok < 3) {
        subghz_devices_idle(rc->dev);
        return;
    }
    app->dec_seen++;
    bool got = decode_session(rc, best_khz, thr);
    rc->dec_cool = furi_get_tick() + (got ? 4000 : 2000);
}

/* ---------- multi use ---------- */

static void multi_step(Ctx* rc, const FsConfig* c) {
    FsApp* app = rc->app;
    const uint8_t need = fs_slots_need(c);
    bool nE = (need & (1 << FsSlotExt)) && rc->ext_open;
    bool nI = (need & (1 << FsSlotInt)) && rc->int_open;
    bool nS = (need & (1 << FsSlotNrfSpec)) && rc->nrf_open;
    const bool nP = (need & (1 << FsSlotNrfPkt)) && rc->nrf_open;
    const bool nW = (need & (1 << FsSlotWifi)) != 0;

    furi_mutex_acquire(app->mtx, FuriWaitForever);
    if(nI && !app->ch[1] && memmgr_get_free_heap() > 12 * 1024)
        app->ch[1] = fs_chan_alloc(FS_WF_ROWS_SEC);
    if(nS && !app->ch[2] && memmgr_get_free_heap() > 12 * 1024)
        app->ch[2] = fs_chan_alloc(FS_WF_ROWS_SEC);
    if(!(need & (1 << FsSlotInt)) && app->ch[1]) {
        fs_chan_free(app->ch[1]);
        app->ch[1] = NULL;
        rc->have_geo[1] = false;
    }
    if(!(need & (1 << FsSlotNrfSpec)) && app->ch[2]) {
        fs_chan_free(app->ch[2]);
        app->ch[2] = NULL;
        rc->have_geo[2] = false;
    }
    furi_mutex_release(app->mtx);
    nI = nI && app->ch[1];
    nS = nS && app->ch[2];

    if(nW && !rc->wio) {
        wifi_open(rc, c);
    } else if(!nW && rc->wio) {
        wifi_close(rc);
    }

    FsGeo gE = {0}, gI = {0}, gS = {0};
    if(nE) fs_geo_compute(c, FsRadioExt, 0, true, &gE);
    if(nI) fs_geo_compute(c, FsRadioInt, 1, true, &gI);
    if(nS) fs_geo_compute(c, FsRadioNrf, 2, true, &gS);
    cc_ensure_multi(rc, c, nE ? &gE : NULL, nI ? &gI : NULL);

    bool worked = false;
    bool cc_done = false;
    if(nE && nI) {
        if(!cc_multi_sweep(rc, c, &gE, &gI)) return;
        publish_chan(rc, 0, c, &gE);
        publish_chan(rc, 1, c, &gI);
        worked = cc_done = true;
    } else if(nE) {
        if(!cc_sweep_dev(rc, c, &gE, rc->dev_ext, 0, c->off_ext + c->rssi_off)) return;
        publish_chan(rc, 0, c, &gE);
        worked = cc_done = true;
    } else if(nI) {
        if(!cc_sweep_dev(rc, c, &gI, rc->dev_int, 1, c->off_int + c->rssi_off)) return;
        publish_chan(rc, 1, c, &gI);
        worked = cc_done = true;
    }

    const bool do_sweep = nS && (!nP || rc->nrf_toggle == 0);
    const bool do_pkt = nP && (!nS || rc->nrf_toggle == 1);
    if(nS && nP) rc->nrf_toggle ^= 1;
    if(do_sweep) {
        if(!nrf_sweep(rc, c, &gS, 2)) return;
        publish_chan(rc, 2, c, &gS);
        worked = true;
    }
    if(do_pkt) {
        nrf_pkt_slice(rc, c);
        worked = true;
    }

    if(rc->wio) wifi_poll(rc, c);
    if(cc_done && c->auto_decode && !app->ui.freeze) decode_step(rc, c);
    if(!app->boot_done) {
        BOOT(100, "READY");
        app->boot_done = true;
    }
    furi_delay_ms(worked ? 1 : 25);
}

/* ---------- SDR mode (GQRX bridge) ---------- */

static void sdr_step(Ctx* rc, FsConfig* c) {
    FsApp* app = rc->app;
    fs_sdr_poll(app);
    if(rc->kind == FsRadioNrf || !rc->dev) {
        app->sdr_state = 3;
        furi_delay_ms(40);
        return;
    }
    if(app->sdr_state != 2) {
        furi_delay_ms(20);
        return;
    }
    const uint8_t gs = app->sdr_gain;
    c->gain_lna = gs > 100 ? 0 : (uint8_t)(8 - (gs * 7 + 50) / 100);
    if(gs != rc->sdr_gain_seen) {
        rc->sdr_gain_seen = gs;
        rc->hw_mode = -1;
    }
    c->auto_decode = 0;
    uint32_t span = app->sdr_span_khz;
    if(span < 100) span = 100;
    if(span > 12000) span = 12000;
    uint32_t center = app->sdr_center_khz;
    FsGeo g;
    g.span_khz = span;
    g.start_khz = center > span / 2 ? center - span / 2 : 0;
    g.nmeas = FS_BINS;
    g.fam = 0;
    cc_ensure(rc, c, FsModeSweep, &g, &g);
    uint32_t t0 = furi_get_tick();
    if(cc_sweep(rc, c, &g)) {
        fs_sdr_send_frame(
            app, rc->meas[0], FS_BINS, center, span, (uint16_t)(furi_get_tick() - t0));
    }
    furi_delay_ms(1);
}
/* ---------- thread ---------- */

static int32_t worker_thread(void* ctx) {
    FsApp* app = ctx;
    Ctx* rc = malloc(sizeof(Ctx));
    memset(rc, 0, sizeof(Ctx));
    rc->app = app;
    rc->hw_mode = -1;
    rc->hw_lock_ch = -1;
    rc->prev_mode = -1;
    app->worker_alive = true;

    FsConfig c;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    c = app->cfg;
    furi_mutex_release(app->mtx);

    bool ok = radio_open(rc, &c);
    rc->hw_rev = app->hw_rev;
    FURI_LOG_I(TAG, "radio open: %d kind=%d pg=%d", ok, rc->kind, rc->pg);
    {
        char tb[40];
        snprintf(tb, sizeof(tb), "worker: radio open=%d kind=%d", ok, rc->kind);
        fs_trace(app, tb);
    }
    if(!ok) {
        BOOT(100, "NO RADIO FOUND");
        app->boot_done = true;
    } else {
        if(c.auto_decode && rc->kind != FsRadioNrf) {
            BOOT(86, "AUTO DECODE ENGINE");
            if(!decode_alloc(rc)) {
                furi_mutex_acquire(app->mtx, FuriWaitForever);
                app->cfg.auto_decode = 0;
                furi_mutex_release(app->mtx);
                c.auto_decode = 0;
                fs_worker_post(app, "DECODE: LOW MEMORY");
            }
        }
        BOOT(92, "FIRST SWEEP");
    }

    while(app->worker_run) {
        if(furi_mutex_acquire(app->mtx, 200) != FuriStatusOk) continue;
        c = app->cfg;
        int mode = app->worker_mode;
        furi_mutex_release(app->mtx);
        rc->raw_mode = mode;
        const bool sdr_want = ok && mode == FsModeSdr;
        const bool multi = ok && app->multi_active && !sdr_want;
        if(multi) mode = FsModeSweep;
        rc->cur_mode = mode;

        if(!ok) {
            if(mode == FsModeSdr) app->sdr_state = 3;
            app->cap_req = false;
            app->log_req = 0;
            furi_delay_ms(40);
            continue;
        }

        FsGeo g0;
        fs_geo_compute(&c, rc->kind, 0, multi, &g0);

        if(app->log_req == 1 && !rc->logf) log_open(rc, &c, &g0);
        if(app->log_req == 2) {
            log_close(rc);
            fs_worker_post(app, "LOG SAVED");
        }
        app->log_req = 0;

        if(app->cap_req) {
            do_capture(rc, &c);
            continue;
        }

        if(!c.auto_decode && rc->dec) decode_free(rc);

        if(sdr_want && !rc->sdr_on) {
            fs_sdr_start(app);
            rc->sdr_on = true;
            rc->sdr_gain_seen = 255;
        }
        if(!sdr_want && rc->sdr_on) {
            fs_sdr_stop(app);
            rc->sdr_on = false;
            rc->hw_mode = -1;
        }
        if(sdr_want) {
            sdr_step(rc, &c);
            continue;
        }
        if(!multi && rc->wio) wifi_close(rc);
        if(multi) {
            multi_step(rc, &c);
            continue;
        }

        if(rc->prev_mode != mode) {
            rc->hw_lock_ch = -1;
            rc->prev_mode = mode;
        }

        if(rc->kind == FsRadioNrf) {
            rc->hw_rev = app->hw_rev;
            if(mode == FsModeSweep) {
                if(nrf_sweep(rc, &c, &g0, 0)) publish_chan(rc, 0, &c, &g0);
                furi_delay_ms(1);
            } else {
                nrf_lock_step(rc, &c);
            }
        } else {
            cc_ensure(rc, &c, mode, &g0, &g0);
            if(mode == FsModeSweep) {
                bool done;
                if(rc->pg && rc->dev_ext && rc->int_open)
                    done = cc_merged_sweep(rc, &c, &g0);
                else
                    done = cc_sweep(rc, &c, &g0);
                if(done) publish_chan(rc, 0, &c, &g0);
                if(done && c.auto_decode && !app->ui.freeze) decode_step(rc, &c);
                furi_delay_ms(1);
            } else if(mode == FsModeLock) {
                cc_lock_step(rc, &c);
            } else {
                cc_scope_step(rc, &c);
            }
        }
        if(!app->boot_done && mode != FsModeSweep) {
            BOOT(100, "READY");
            app->boot_done = true;
        }
    }

    log_close(rc);
    if(rc->sdr_on) fs_sdr_stop(app);
    radio_close(rc);
    if(rc->burst) {
        free(rc->burst);
        rc->burst = NULL;
    }
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    for(int i = 1; i < 3; i++) {
        if(app->ch[i]) {
            fs_chan_free(app->ch[i]);
            app->ch[i] = NULL;
        }
    }
    app->multi_active = false;
    furi_mutex_release(app->mtx);
    free(rc);
    app->worker_alive = false;
    return 0;
}

void fs_worker_start(FsApp* app) {
    app->status = FsStatusStarting;
    app->radio = FsRadioNone;
    app->worker_run = true;
    app->worker = furi_thread_alloc_ex("FlipSayRadio", 6144, worker_thread, app);
    furi_thread_start(app->worker);
}

void fs_worker_stop(FsApp* app) {
    if(!app->worker) return;
    app->worker_run = false;
    FuriThread* t = app->worker;
    app->worker = NULL;
    furi_thread_join(t);
    furi_thread_free(t);
}
