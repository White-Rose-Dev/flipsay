#include "flipsay.h"

#include <stdio.h>
#include <string.h>

static const int8_t scale_ref[7] = {0, -20, -30, -40, -50, -60, -70};
static const int8_t scale_rng[7] = {0, 100, 80, 70, 60, 50, 40};

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

FsChan* fs_chan_alloc(uint8_t wf_rows) {
    FsChan* c = malloc(sizeof(FsChan));
    memset(c, 0, sizeof(FsChan));
    c->wf_rows = wf_rows;
    c->wf = malloc((size_t)wf_rows * (FS_BINS / 2));
    memset(c->wf, 0, (size_t)wf_rows * (FS_BINS / 2));
    fs_chan_reset(c);
    return c;
}

void fs_chan_free(FsChan* c) {
    if(!c) return;
    if(c->heat) free(c->heat);
    if(c->wf) free(c->wf);
    free(c);
}

void fs_chan_reset(FsChan* c) {
    memset(c->raw, 0, sizeof(c->raw));
    memset(c->trace, 0, sizeof(c->trace));
    memset(c->avg_fx, 0, sizeof(c->avg_fx));
    memset(c->slow, 0, sizeof(c->slow));
    memset(c->slow_fx, 0, sizeof(c->slow_fx));
    memset(c->hold, 0, sizeof(c->hold));
    if(c->wf) memset(c->wf, 0, (size_t)c->wf_rows * (FS_BINS / 2));
    if(c->heat) memset(c->heat, 0, FS_BINS * FS_PLOT_H);
    c->wf_head = 0;
    c->wf_count = 0;
    c->fresh = true;
    c->n_peaks = 0;
    c->noise_fx = 0;
    c->noise = 0;
    c->lo16 = 40 << 4;
    c->hi16 = 200 << 4;
    c->wf_lo16 = 20 << 4;
    c->wf_hi16 = 120 << 4;
    c->duty = 0;
    c->trig_active = false;
}

uint8_t fs_dbm_to_lvl(int dbm) {
    return (uint8_t)clampi(((dbm + 120) * 255 + 50) / 100, 0, 255);
}

int fs_lvl_to_dbm(int lvl) {
    return (lvl * 100 + 127) / 255 - 120;
}

uint8_t fs_sq_level(const FsConfig* c, int fam) {
    if(c->squelch == 0) return 0;
    if(fam) return (uint8_t)clampi(c->squelch * 6 * 255 / 100, 1, 255);
    return fs_dbm_to_lvl(-105 + 5 * (c->squelch - 1));
}

void fs_scale(const FsApp* app, const FsChan* ch, int fam, int* lo, int* hi) {
    int l, h;
    if(fam) {
        *lo = 0;
        *hi = 255;
        return;
    }
    uint8_t s = app->cfg.scale;
    if(s == 0 || s > 6) {
        l = ch->lo16 >> 4;
        h = ch->hi16 >> 4;
    } else {
        h = (scale_ref[s] + 120) * 255 / 100;
        l = h - scale_rng[s] * 255 / 100;
    }
    l = clampi(l, 0, 239);
    h = clampi(h, l + 16, 255);
    *lo = l;
    *hi = h;
}

static void norm_span(uint32_t* center, uint32_t* span, uint32_t minf, uint32_t maxf, uint32_t minspan) {
    uint32_t maxspan = maxf - minf;
    uint32_t sp = *span;
    if(sp < minspan) sp = minspan;
    if(sp > maxspan) sp = maxspan;
    uint32_t cen = *center;
    if(cen < minf + sp / 2) cen = minf + sp / 2;
    if(cen > maxf - sp / 2) cen = maxf - sp / 2;
    *span = sp;
    *center = cen;
}

void fs_geo_normalize(FsConfig* c, int fam) {
    if(fam)
        norm_span(&c->center_khz[1], &c->span_khz[1], 2400000UL, 2527000UL, 4000UL);
    else
        norm_span(&c->center_khz[0], &c->span_khz[0], 281000UL, 962000UL, 50UL);
}

void fs_geo_normalize_int(FsConfig* c) {
    norm_span(&c->int_center_khz, &c->int_span_khz, 281000UL, 962000UL, 50UL);
}

void fs_geo_compute(const FsConfig* c, FsRadio kind, int win, bool multi, FsGeo* g) {
    int fam = (kind == FsRadioNrf) ? 1 : 0;
    uint32_t center, span;
    if(win == 2) {
        FsConfig t = *c;
        fs_geo_normalize(&t, 1);
        center = t.center_khz[1];
        span = t.span_khz[1];
        fam = 1;
    } else if(win == 1) {
        center = c->int_center_khz;
        span = c->int_span_khz;
        norm_span(&center, &span, 281000UL, 962000UL, 50UL);
        fam = 0;
    } else {
        FsConfig t = *c;
        if(fam && c->view == FsViewChannels && !multi) {
            t.center_khz[1] = 2463500UL;
            t.span_khz[1] = 127000UL;
        }
        fs_geo_normalize(&t, fam);
        center = t.center_khz[fam];
        span = t.span_khz[fam];
    }
    g->span_khz = span;
    g->start_khz = center - span / 2;
    g->nmeas = (c->res || multi) ? 64 : 128;
    g->fam = (uint8_t)fam;
}

int fs_slot_count(int layout) {
    static const uint8_t n[5] = {2, 2, 3, 3, 4};
    return n[layout > 4 ? 0 : layout];
}

uint8_t fs_slots_need(const FsConfig* c) {
    uint8_t m = 0;
    int n = fs_slot_count(c->mu_layout);
    for(int i = 0; i < n; i++) {
        if(c->mu_slot[i] > 0 && c->mu_slot[i] < FsSlotCount) m |= (uint8_t)(1 << c->mu_slot[i]);
    }
    return m;
}

uint32_t fs_col_khz(const FsGeo* g, int col) {
    col = clampi(col, 0, FS_BINS - 1);
    return g->start_khz + (g->span_khz * (uint32_t)col) / (FS_BINS - 1);
}

int fs_khz_col(const FsGeo* g, uint32_t khz) {
    if(khz <= g->start_khz || g->span_khz == 0) return 0;
    uint32_t d = khz - g->start_khz;
    if(d >= g->span_khz) return FS_BINS - 1;
    return (int)((d * (FS_BINS - 1) + g->span_khz / 2) / g->span_khz);
}

void fs_dsp_process(FsApp* app, FsChan* ch, const FsConfig* c, bool primary) {
    static const uint8_t hold_dec[5] = {255, 1, 3, 8, 0};
    static const uint8_t wf_rates[4] = {1, 2, 4, 8};
    const int fam = ch->geo.fam;
    const uint8_t shift = c->avg > 4 ? 4 : c->avg;
    uint8_t hist[256];
    memset(hist, 0, sizeof(hist));
    int n_valid = 0;
    uint8_t maxv = 0, maxc = 0;

    for(int i = 0; i < FS_BINS; i++) {
        if(!ch->valid[i]) {
            ch->trace[i] = 0;
            ch->slow[i] = 0;
            ch->hold[i] = 0;
            continue;
        }
        int32_t t = (int32_t)ch->raw[i] << 8;
        if(ch->fresh || shift == 0) {
            ch->avg_fx[i] = (uint16_t)t;
        } else {
            int32_t a = ch->avg_fx[i];
            a += (t - a) >> shift;
            ch->avg_fx[i] = (uint16_t)a;
        }
        uint8_t tr = (uint8_t)(ch->avg_fx[i] >> 8);
        ch->trace[i] = tr;

        if(ch->fresh) {
            ch->slow_fx[i] = (uint16_t)t;
        } else {
            int32_t s = ch->slow_fx[i];
            s += (t - s) >> 5;
            ch->slow_fx[i] = (uint16_t)s;
        }
        ch->slow[i] = (uint8_t)(ch->slow_fx[i] >> 8);

        if(ch->fresh || c->hold == 0 || tr > ch->hold[i]) {
            ch->hold[i] = tr;
        } else if(c->hold < 4) {
            uint8_t d = hold_dec[c->hold];
            ch->hold[i] = ch->hold[i] > d ? ch->hold[i] - d : 0;
            if(ch->hold[i] < tr) ch->hold[i] = tr;
        }

        hist[tr]++;
        n_valid++;
        if(tr >= maxv) {
            maxv = tr;
            maxc = (uint8_t)i;
        }
    }
    ch->fresh = false;
    ch->trace_max = maxv;
    ch->max_col = maxc;

    if(n_valid > 0) {
        int target = n_valid * 30 / 100;
        int acc = 0, nf = 0;
        for(int v = 0; v < 256; v++) {
            acc += hist[v];
            if(acc > target) {
                nf = v;
                break;
            }
        }
        if(ch->noise_fx == 0) {
            ch->noise_fx = (uint16_t)(nf << 4);
        } else {
            int32_t d = ((int32_t)nf << 4) - (int32_t)ch->noise_fx;
            ch->noise_fx = (uint16_t)((int32_t)ch->noise_fx + (d >> 3));
        }
        ch->noise = (uint8_t)(ch->noise_fx >> 4);
    }

    const int margin = fam ? 40 : 24;
    const int thr = ch->noise + margin;
    uint8_t cc[40], cl[40];
    int nc = 0;
    for(int i = 0; i < FS_BINS && nc < 40; i++) {
        if(!ch->valid[i]) continue;
        int l = ch->trace[i];
        if(l < thr) continue;
        int left = (i > 0 && ch->valid[i - 1]) ? ch->trace[i - 1] : 0;
        int right = (i < FS_BINS - 1 && ch->valid[i + 1]) ? ch->trace[i + 1] : 0;
        if(l >= left && l > right) {
            cc[nc] = (uint8_t)i;
            cl[nc] = (uint8_t)l;
            nc++;
        }
    }
    ch->n_peaks = 0;
    while(ch->n_peaks < FS_MAX_PEAKS) {
        int best = -1;
        for(int k = 0; k < nc; k++) {
            if(cl[k] == 0) continue;
            if(best < 0 || cl[k] > cl[best]) best = k;
        }
        if(best < 0) break;
        ch->peaks[ch->n_peaks].col = cc[best];
        ch->peaks[ch->n_peaks].lvl = cl[best];
        ch->n_peaks++;
        int bc = cc[best];
        for(int k = 0; k < nc; k++) {
            int d = (int)cc[k] - bc;
            if(d < 0) d = -d;
            if(d < 4) cl[k] = 0;
        }
    }

    ch->obw_khz = 0;
    if(ch->n_peaks > 0) {
        int pc = ch->peaks[0].col;
        int pl = ch->peaks[0].lvl;
        int half = ch->noise + (pl - ch->noise) / 2;
        int l = pc, r = pc;
        while(l > 0 && ch->valid[l - 1] && ch->trace[l - 1] >= half) l--;
        while(r < FS_BINS - 1 && ch->valid[r + 1] && ch->trace[r + 1] >= half) r++;
        ch->obw_khz = ((uint32_t)(r - l + 1) * ch->geo.span_khz) / (FS_BINS - 1);
    }

    if(n_valid > 0) {
        static const uint8_t wave_min[3] = {64, 40, 24};
        static const uint8_t wave_mar[3] = {14, 9, 5};
        int wv = c->wave > 2 ? 0 : c->wave;
        int tlo = clampi((int)ch->noise - wave_mar[wv], 0, 255);
        int thi = clampi((int)maxv + wave_mar[wv], 0, 255);
        if(thi < tlo + wave_min[wv]) thi = tlo + wave_min[wv];
        if(thi > 255) {
            thi = 255;
            tlo = 255 - wave_min[wv];
        }
        int lo = ch->lo16, hi = ch->hi16;
        lo += ((tlo << 4) - lo) >> 2;
        hi += ((thi << 4) - hi) >> 2;
        ch->lo16 = (int16_t)lo;
        ch->hi16 = (int16_t)hi;

        int wl = clampi((int)ch->noise + 2, 0, 200);
        int wh = clampi((int)maxv, wl + 40, 255);
        int a = ch->wf_lo16, b = ch->wf_hi16;
        a += ((wl << 4) - a) >> 2;
        b += ((wh << 4) - b) >> 2;
        ch->wf_lo16 = (int16_t)a;
        ch->wf_hi16 = (int16_t)b;
    }

    uint32_t now = furi_get_tick();
    if(primary) {
        uint8_t sq = fs_sq_level(c, fam);
        bool act = (sq != 0) && (n_valid > 0) && (maxv >= sq);
        if(act && !ch->trig_active && (now - ch->trig_last) > 2000) {
            ch->hits++;
            app->trig_khz = fs_col_khz(&ch->geo, maxc);
            app->trig_lvl = maxv;
            ch->trig_last = now;
            app->trig_event = true;
        }
        ch->trig_active = act;
        int d = (act ? 1000 : 0) - (int)ch->duty;
        ch->duty = (uint16_t)((int)ch->duty + d / 32);
    }

    if(ch->wf && ch->sweeps % wf_rates[c->wf_rate & 3] == 0) {
        int wlo = ch->wf_lo16 >> 4;
        int wrng = (ch->wf_hi16 >> 4) - wlo;
        if(wrng < 8) wrng = 8;
        uint8_t* wrow = ch->wf + (size_t)ch->wf_head * (FS_BINS / 2);
        for(int i = 0; i < FS_BINS; i += 2) {
            int s0 = clampi(((int)ch->trace[i] - wlo) * 15 / wrng, 0, 15);
            int s1 = clampi(((int)ch->trace[i + 1] - wlo) * 15 / wrng, 0, 15);
            wrow[i / 2] = (uint8_t)((s0 << 4) | s1);
        }
        ch->wf_head = (uint8_t)((ch->wf_head + 1) % ch->wf_rows);
        if(ch->wf_count < ch->wf_rows) ch->wf_count++;
    }

    if(ch->heat) {
        int lo, hi;
        fs_scale(app, ch, fam, &lo, &hi);
        for(int i = 0; i < FS_BINS * FS_PLOT_H; i++) {
            uint8_t h = ch->heat[i];
            if(h) ch->heat[i] = (uint8_t)(h - (h >> 3) - 1);
        }
        int prev_y = -1;
        for(int x = 0; x < FS_BINS; x++) {
            if(!ch->valid[x]) {
                prev_y = -1;
                continue;
            }
            int y = clampi(((int)ch->trace[x] - lo) * (FS_PLOT_H - 1) / (hi - lo), 0, FS_PLOT_H - 1);
            if(prev_y >= 0) {
                int a = prev_y < y ? prev_y : y;
                int b = prev_y < y ? y : prev_y;
                for(int yy = a + 1; yy < b; yy++) {
                    int idx = x * FS_PLOT_H + yy;
                    int v = ch->heat[idx] + 40;
                    ch->heat[idx] = (uint8_t)(v > 255 ? 255 : v);
                }
            }
            int idx = x * FS_PLOT_H + y;
            int v = ch->heat[idx] + 100;
            ch->heat[idx] = (uint8_t)(v > 255 ? 255 : v);
            prev_y = y;
        }
    }

    if(ch->sweep_last) {
        uint32_t d = now - ch->sweep_last;
        ch->sweep_ms = (ch->sweep_ms * 3 + d) / 4;
    }
    ch->sweep_last = now;
    ch->sweeps++;
}

int fs_snr(const FsChan* ch) {
    int d = (int)ch->trace_max - (int)ch->noise;
    if(d < 0) d = 0;
    return d * 100 / 255;
}

typedef struct {
    uint32_t a;
    uint32_t b;
    const char* name;
} FsLbl;

static const FsLbl labels[] = {
    {314850, 315150, "315 US"},
    {317850, 318150, "318 US"},
    {309850, 310150, "310 GAR"},
    {344850, 345150, "345 SEC"},
    {389850, 390150, "390 GAR"},
    {433800, 434050, "433.92"},
    {433050, 434790, "433 ISM"},
    {868000, 868600, "868 SRD"},
    {863000, 870000, "868 EU"},
    {902000, 928000, "915 ISM"},
    {2401000, 2403000, "BLE 37"},
    {2425000, 2427000, "BLE 38"},
    {2479000, 2481000, "BLE 39"},
};

void fs_label(uint32_t khz, int fam, char* out, size_t n) {
    out[0] = 0;
    for(size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if(khz >= labels[i].a && khz <= labels[i].b) {
            snprintf(out, n, "%s", labels[i].name);
            return;
        }
    }
    if(fam && khz >= 2400000UL && khz <= 2497000UL) {
        int ch = (int)((khz + 2500UL - 2407000UL) / 5000UL);
        if(ch < 1) ch = 1;
        if(ch > 14) ch = 14;
        snprintf(out, n, "WIFI %d", ch);
    }
}
