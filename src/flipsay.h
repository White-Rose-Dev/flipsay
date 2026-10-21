#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <lib/subghz/devices/devices.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>

#include "flipsay_gfx.h"

#define FS_VERSION     "0.2.1"
#define FS_BINS        128
#define FS_WF_ROWS     40
#define FS_WF_ROWS_SEC 16
#define FS_PLOT_H      47
#define FS_MAX_PEAKS   8
#define FS_SCOPE_MAX   400
#define FS_HIST        128
#define FS_ACT_COLS    128
#define FS_MAX_BM      12
#define FS_CAP_MAX     2048
#define FS_MAX_PKTS    6
#define FS_SAVED_MAX   24
#define FS_SAVED_NAME  30

#define FS_DIR           EXT_PATH("apps_data/flipsay")
#define FS_SETTINGS_FILE FS_DIR "/settings.bin"
#define FS_BM_FILE       FS_DIR "/bookmarks.bin"
#define FS_SAVED_DIR     EXT_PATH("subghz/FlipSay")

#define FS_CFG_MAGIC 0x59415346UL

typedef enum {
    FsSrcAuto,
    FsSrcInt,
    FsSrcExt,
    FsSrcNrf,
    FsSrcCount,
} FsSource;

typedef enum {
    FsRadioNone,
    FsRadioInt,
    FsRadioExt,
    FsRadioNrf,
} FsRadio;

typedef enum {
    FsStatusStarting,
    FsStatusOk,
    FsStatusNoExt,
    FsStatusNoNrf,
    FsStatusNoPg,
    FsStatusFail,
} FsStatus;

typedef enum {
    FsViewSpectrum,
    FsViewWaterfall,
    FsViewCombo,
    FsViewPersist,
    FsViewScope,
    FsViewMeter,
    FsViewSignals,
    FsViewChannels,
    FsViewCount,
} FsView;

typedef enum {
    FsModeSweep,
    FsModeLock,
    FsModeScope,
    FsModeSdr,
} FsMode;

typedef enum {
    FsUiSplash,
    FsUiView,
    FsUiMenu,
    FsUiBookmarks,
    FsUiHelp,
    FsUiPins,
    FsUiWarn,
    FsUiSaved,
    FsUiSavedInfo,
    FsUiExiting,
    FsUiAbout,
    FsUiSdr,
} FsUiMode;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t source;
    uint8_t view;
    uint8_t res;
    uint8_t speed;
    uint8_t bw;
    uint8_t avg;
    uint8_t hold;
    uint8_t scale;
    uint8_t style;
    uint8_t overlay;
    uint8_t grid;
    uint8_t wf_pal;
    uint8_t wf_rate;
    uint8_t trig_act;
    uint8_t alerts;
    uint8_t backlight;
    uint8_t otg;
    uint8_t mod;
    uint8_t scope_gap;
    uint8_t nrf_rate;
    uint8_t track;
    uint8_t step_idx;
    uint8_t ch_page;
    uint8_t squelch;
    uint8_t wave;
    uint8_t gain_lna;
    uint8_t gain_dvga;
    uint8_t gain_tgt;
    uint8_t pin_cc;
    uint8_t pin_nrf;
    uint8_t auto_decode;
    uint8_t notify;
    uint8_t dec_sens;
    uint8_t pg_mode;
    uint8_t pg_combine;
    uint8_t mu_nrf_speed;
    uint8_t tut_done;
    uint8_t mu_layout;
    uint8_t mu_slot[4];
    uint8_t wifi_mode;
    uint8_t wifi_view;
    uint8_t lite;
    uint8_t intro;
    uint8_t preset[2];
    int8_t rssi_off;
    int8_t off_int;
    int8_t off_ext;
    uint8_t pad[3];
    uint32_t center_khz[2];
    uint32_t span_khz[2];
    uint32_t lock_khz[2];
    uint32_t int_center_khz;
    uint32_t int_span_khz;
} FsConfig;

typedef struct {
    uint32_t start_khz;
    uint32_t span_khz;
    uint8_t nmeas;
    uint8_t fam;
} FsGeo;

typedef struct {
    uint8_t col;
    uint8_t lvl;
} FsPeak;

typedef struct {
    uint16_t dur;
    uint8_t lvl;
} FsPulse;

typedef struct {
    uint8_t addr[5];
    uint8_t ch;
    uint8_t plen;
    uint8_t pay[7];
    uint16_t count;
    uint32_t last;
} FsPkt;

typedef enum {
    FsSlotOff,
    FsSlotExt,
    FsSlotInt,
    FsSlotNrfSpec,
    FsSlotNrfPkt,
    FsSlotWifi,
    FsSlotCount,
} FsSlot;

#define FS_MAX_APS 10

typedef struct {
    char ssid[17];
    int8_t rssi;
    uint8_t ch;
    uint8_t bssid_tail[3];
    uint32_t last;
} FsAp;

typedef enum {
    FsWifiOff,
    FsWifiStarting,
    FsWifiNoResponse,
    FsWifiNotMarauder,
    FsWifiBusy,
    FsWifiReady,
} FsWifiState;

typedef struct FsWifi {
    uint8_t state;
    char ver[12];
    FsAp aps[FS_MAX_APS];
    uint8_t n_aps;
    uint32_t total;
    uint16_t rate;
    uint8_t hist[32];
    uint8_t hist_head;
    char log[4][30];
    uint8_t log_head;
} FsWifi;

typedef struct {
    FsGeo geo;
    uint8_t raw[FS_BINS];
    uint8_t valid[FS_BINS];
    uint8_t trace[FS_BINS];
    uint8_t hold[FS_BINS];
    uint8_t slow[FS_BINS];
    uint16_t avg_fx[FS_BINS];
    uint16_t slow_fx[FS_BINS];
    bool fresh;
    uint8_t noise;
    uint16_t noise_fx;
    uint8_t trace_max;
    uint8_t max_col;
    FsPeak peaks[FS_MAX_PEAKS];
    uint8_t n_peaks;
    uint32_t obw_khz;
    int16_t lo16;
    int16_t hi16;
    int16_t wf_lo16;
    int16_t wf_hi16;
    uint8_t* wf;
    uint8_t wf_rows;
    uint8_t wf_head;
    uint8_t wf_count;
    uint8_t* heat;
    uint32_t sweeps;
    uint32_t sweep_ms;
    uint32_t sweep_last;
    uint32_t hits;
    uint16_t duty;
    bool trig_active;
    uint32_t trig_last;
} FsChan;

typedef struct {
    uint32_t center_khz;
    uint32_t span_khz;
    uint8_t fam;
    uint8_t src;
    char name[10];
} FsBookmark;

typedef struct {
    uint32_t magic;
    uint32_t count;
    FsBookmark items[FS_MAX_BM];
} FsBookmarks;

typedef struct {
    FsUiMode mode;
    uint32_t mode_tick;
    uint8_t cursor;
    uint32_t marker_b_khz;
    bool freeze;
    uint8_t menu_page;
    uint8_t menu_sel;
    uint8_t menu_top;
    uint8_t bm_sel;
    uint8_t sig_sel;
    uint8_t help_page;
    uint8_t focus;
    uint8_t about_page;
    uint8_t wifi_scroll;
    uint8_t warn_step;
    uint32_t warn_tick;
    uint8_t scroll_rep;
    uint8_t saved_sel;
    uint8_t saved_top;
    uint8_t saved_armed;
    uint32_t saved_arm_tick;
    char toast[28];
    uint32_t toast_until;
    bool dirty;
    uint32_t fade_start;
    uint32_t exit_tick;
    uint32_t splash_out;
    bool splash_skip;
    uint8_t boot_disp;
} FsUi;

typedef enum {
    FsEvInput,
    FsEvTick,
} FsEvType;

typedef struct {
    FsEvType type;
    InputEvent input;
} FsEvent;

typedef struct FsApp FsApp;

struct FsApp {
    Gui* gui;
    ViewPort* vp;
    FuriMessageQueue* queue;
    FuriMutex* mtx;
    NotificationApp* notif;
    Storage* storage;

    FuriThread* worker;
    volatile bool worker_run;
    volatile bool worker_alive;

    FsConfig cfg;
    FsUi ui;
    FsBookmarks bm;

    volatile uint8_t status;
    volatile uint8_t radio;
    volatile uint8_t worker_mode;
    volatile uint32_t hw_rev;
    volatile bool pg_on;
    volatile bool have_ext;
    volatile bool have_nrf;
    volatile bool multi_active;
    volatile uint8_t sdr_state;
    volatile bool sdr_active;
    volatile uint32_t sdr_center_khz;
    volatile uint32_t sdr_span_khz;
    volatile uint8_t sdr_gain;
    volatile uint8_t sdr_gain_applied;
    uint32_t sdr_frames;
    uint16_t sdr_fps10;

    FsChan* ch[3];
    struct FsWifi* wifi;
    uint8_t lut[16];

    volatile bool trig_event;
    uint32_t trig_khz;
    int16_t trig_lvl;

    uint8_t meter_cur;
    uint8_t meter_peak;
    uint16_t meter_fx;
    uint8_t meter_hist[FS_HIST];
    volatile uint8_t meter_head;
    int16_t rssi_dbm;

    FsPulse* scope;
    uint16_t scope_n;
    uint32_t scope_total_us;
    uint32_t scope_min_us;
    uint32_t scope_max_us;
    uint32_t scope_bursts;
    uint8_t scope_hint;
    uint8_t act[FS_ACT_COLS];
    volatile uint8_t act_head;

    FsPkt pkts[FS_MAX_PKTS];
    uint8_t n_pkts;
    uint32_t pkt_total;
    uint16_t pkt_rate;
    uint8_t pkt_ch;

    volatile uint8_t dec_state;
    uint32_t dec_khz;
    uint32_t dec_saved;
    uint32_t dec_seen;

    volatile bool nt_event;
    char nt_title[20];
    char nt_text[36];
    uint8_t nt_kind;
    uint32_t nt_start;

    char (*saved_names)[FS_SAVED_NAME];
    uint8_t saved_n;
    char saved_info[4][32];

    volatile uint8_t log_req;
    volatile bool log_active;
    uint32_t log_rows;
    volatile bool cap_req;
    volatile uint32_t cap_khz;
    volatile bool snap_req;
    volatile bool snap_ready;
    uint8_t* snap_buf;
    char worker_msg[28];
    volatile bool worker_msg_flag;

    volatile uint8_t boot_pct;
    const char* volatile boot_msg;
    volatile bool boot_done;
    FuriThreadId main_id;
    uint8_t orig_spi_cc;
    uint8_t orig_spi_nrf;
    uint32_t last_sig;
    uint32_t last_draw;
    bool restart_req;
    bool running;
};

/* dsp */
FsChan* fs_chan_alloc(uint8_t wf_rows);
int fs_slot_count(int layout);
uint8_t fs_slots_need(const FsConfig* c);
void fs_chan_free(FsChan* c);
void fs_chan_reset(FsChan* c);
uint8_t fs_dbm_to_lvl(int dbm);
int fs_lvl_to_dbm(int lvl);
uint8_t fs_sq_level(const FsConfig* c, int fam);
void fs_scale(const FsApp* app, const FsChan* ch, int fam, int* lo, int* hi);
void fs_geo_normalize(FsConfig* c, int fam);
void fs_geo_normalize_int(FsConfig* c);
void fs_geo_compute(const FsConfig* c, FsRadio kind, int win, bool multi, FsGeo* g);
uint32_t fs_col_khz(const FsGeo* g, int col);
int fs_khz_col(const FsGeo* g, uint32_t khz);
void fs_dsp_process(FsApp* app, FsChan* ch, const FsConfig* c, bool primary);
void fs_label(uint32_t khz, int fam, char* out, size_t n);
int fs_snr(const FsChan* ch);

/* radio */
void fs_worker_start(FsApp* app);
void fs_worker_stop(FsApp* app);
void fs_worker_post(FsApp* app, const char* msg);
void fs_sdr_start(FsApp* app);
void fs_sdr_stop(FsApp* app);
void fs_sdr_poll(FsApp* app);
void fs_sdr_send_frame(FsApp* app, const uint8_t* lv, uint8_t n, uint32_t center, uint32_t span, uint16_t sweep_ms);

/* about / sdr screens */
void fs_about_draw(Canvas* canvas, FsApp* app);
void fs_about_input(FsApp* app, const InputEvent* ev);
void fs_about_open(FsApp* app, bool from_menu);
void fs_sdr_draw(Canvas* canvas, FsApp* app);
void fs_sdr_open(FsApp* app);
void fs_sdr_close(FsApp* app);

/* io */
void fs_cfg_defaults(FsConfig* c);
void fs_cfg_load(FsApp* app);
void fs_cfg_save(FsApp* app);
void fs_bm_load(FsApp* app);
void fs_bm_save(FsApp* app);
bool fs_snapshot_write(FsApp* app);
void fs_io_ensure_dirs(Storage* st);
void fs_trace(FsApp* app, const char* msg);
void fs_trace_reset(FsApp* app);
void fs_saved_load(FsApp* app);
void fs_saved_free(FsApp* app);
bool fs_saved_info(FsApp* app, const char* name);
bool fs_saved_delete(FsApp* app, const char* name);
void fs_saved_delete_all(FsApp* app);

/* ui */
void fs_ui_init(FsApp* app);
void fs_ui_draw(Canvas* canvas, void* ctx);
bool fs_ui_input(FsApp* app, const InputEvent* ev);
bool fs_ui_tick(FsApp* app);
void fs_ui_toast(FsApp* app, const char* msg);
void fs_ui_set_view(FsApp* app, FsView v);
void fs_apply_preset(FsApp* app);
int fs_family(const FsApp* app);
void fs_notify(FsApp* app, uint8_t kind, const char* title, const char* text);
void fs_apply_pins(FsApp* app);
void fs_restore_pins(FsApp* app);

extern const char* const fs_view_names[];
