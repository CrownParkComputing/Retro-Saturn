/*
 * ymir_bridge.h — Plain-C ABI between the Flutter frontend and ymir-core.
 *
 * One opaque YmirInstance wraps ymir::Saturn plus a worker thread, a
 * framebuffer double-buffer, an audio ring buffer and a mailbox for
 * synchronous state changes. All mutating calls are thread-safe.
 *
 * Conventions:
 *   - Functions returning int32_t: 0 = ok, negative = error code.
 *   - Framebuffer is XRGB8888 little-endian (byte order 0xAARRGGBB).
 *     Valid only until the next ymir_bridge_run_frame or destroy call.
 *   - Status strings are owned by the instance and valid until the
 *     next call to ymir_bridge_get_status.
 *   - Audio never crosses this boundary — the bridge drives a native
 *     audio backend (ALSA/AAudio/CoreAudio) directly from the SCSP
 *     sample callback. ymir_bridge_get_audio_level exposes a 0..100
 *     peak for UI meters.
 */
#ifndef YMIR_BRIDGE_H
#define YMIR_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- handle ---- */
typedef struct YmirInstance YmirInstance;

/* ---- error codes ---- */
#define YMIR_OK                       0
#define YMIR_ERR_GENERIC             -1
#define YMIR_ERR_INVALID_HANDLE      -2
#define YMIR_ERR_INVALID_ARG         -3
#define YMIR_ERR_FILE_OPEN           -4
#define YMIR_ERR_FILE_READ           -5
#define YMIR_ERR_FILE_WRITE          -6
#define YMIR_ERR_BAD_FORMAT          -7
#define YMIR_ERR_SNAPSHOT_TIMEOUT   -8
#define YMIR_ERR_SNAPSHOT_BAD_MAGIC -9
#define YMIR_ERR_SNAPSHOT_VERSION   -10
#define YMIR_ERR_BUSY               -11
#define YMIR_ERR_NOT_RUNNING        -12

/* ---- peripheral types (matches ymir::peripheral::PeripheralType) ---- */
typedef enum {
    YMIR_PERIPHERAL_NONE          = 0,
    YMIR_PERIPHERAL_CONTROL_PAD   = 1,
    YMIR_PERIPHERAL_ANALOG_PAD    = 2,
    YMIR_PERIPHERAL_ARCADE_RACER  = 3,
    YMIR_PERIPHERAL_MISSION_STICK = 4,
    YMIR_PERIPHERAL_VIRTUA_GUN    = 5,
    YMIR_PERIPHERAL_SHUTTLE_MOUSE = 6,
} YmirPeripheralType;


/* Shuttle Mouse buttons. Distinct from YmirButton: the mouse is its own
 * device, and routing it through the pad mask meant a pad binding moved
 * the pointer. */
typedef enum {
    YMIR_MOUSE_LEFT   = 0,
    YMIR_MOUSE_MIDDLE = 1,
    YMIR_MOUSE_RIGHT  = 2,
    YMIR_MOUSE_START  = 3,
} YmirMouseButton;

/* ---- save state wire format ---- */
#define YMIR_SAVE_STATE_MAGIC   "YMS1"
#define YMIR_SAVE_STATE_VERSION  1u

/* SMPC persistent state file size (matches AndroidYmirRuntime::kSmpcPersistentStateSize) */
#define YMIR_SMPC_STATE_SIZE     25

/* Saturn pad button indices — used with ymir_bridge_set_pad_button */
typedef enum {
    YMIR_BUTTON_UP    = 0,
    YMIR_BUTTON_DOWN  = 1,
    YMIR_BUTTON_LEFT  = 2,
    YMIR_BUTTON_RIGHT = 3,
    YMIR_BUTTON_START = 4,
    YMIR_BUTTON_A     = 5,
    YMIR_BUTTON_B     = 6,
    YMIR_BUTTON_C     = 7,
    YMIR_BUTTON_X     = 8,
    YMIR_BUTTON_Y     = 9,
    YMIR_BUTTON_Z     = 10,
    YMIR_BUTTON_L     = 11,
    YMIR_BUTTON_R     = 12,
    YMIR_BUTTON_COUNT = 13,
} YmirButton;

/* ============================================================ */
/*  lifecycle                                                   */
/* ============================================================ */

/* Create a Saturn instance. Starts the worker thread. Returns NULL on OOM. */
YmirInstance *ymir_bridge_create(void);

/* Destroy and free. Stops the worker thread, drains audio, frees resources. */
void ymir_bridge_destroy(YmirInstance *inst);

/* ============================================================ */
/*  media                                                       */
/* ============================================================ */

/* Load the IPL (BIOS) from path. 512 KiB. Caller passes the absolute path.
 * Triggers a hard reset on success. */
int32_t ymir_bridge_load_bios(YmirInstance *inst, const char *path);

/* Load a disc image (CHD/CUE/MDS/CCD/ISO autodetected via ymir::media::LoadDisc).
 * Triggers a hard reset on success. */
int32_t ymir_bridge_load_disc(YmirInstance *inst, const char *path);

/* Load internal backup RAM (the 32 KiB battery-backed SRAM). */
int32_t ymir_bridge_load_internal_backup_memory(YmirInstance *inst, const char *path,
                                                int32_t copy_on_write);

/* Save internal backup RAM to path. */
int32_t ymir_bridge_save_internal_backup_memory(YmirInstance *inst, const char *path);

/* Load/save SMPC persistent state (the 25-byte file that preserves RTC
 * time, language, region, etc. across power cycles). */
int32_t ymir_bridge_load_smpc_state(YmirInstance *inst, const char *path);
int32_t ymir_bridge_save_smpc_state(YmirInstance *inst, const char *path);

/* ============================================================ */
/*  emulation                                                   */
/* ============================================================ */

/* Run one frame synchronously. Returns the frame count advanced or
 * a negative error. The worker thread runs this in a loop; callers
 * from outside the worker can poll or call directly. */
int32_t ymir_bridge_run_frame(YmirInstance *inst);

/* Hard or soft reset. */
void ymir_bridge_reset(YmirInstance *inst, int32_t hard);

/* Pause/resume the presentation (audio always plays regardless).
 * When paused, the worker thread keeps running frames but the
 * frontend should not advance any UI counter. */
void ymir_bridge_set_presentation_paused(YmirInstance *inst, int32_t paused);
int32_t ymir_bridge_get_presentation_paused(YmirInstance *inst);

/* ============================================================ */
/*  audio                                                       */
/* ============================================================ */

void  ymir_bridge_set_audio_muted(YmirInstance *inst, int32_t muted);
int32_t ymir_bridge_get_audio_muted(YmirInstance *inst);
int32_t ymir_bridge_get_audio_level(YmirInstance *inst);   /* 0..100 smoothed peak */

/* ============================================================ */
/*  status                                                      */
/* ============================================================ */

int32_t     ymir_bridge_get_fps(YmirInstance *inst);
const char *ymir_bridge_get_status(YmirInstance *inst);    /* owned; valid until next call */

/* ============================================================ */
/*  framebuffer                                                 */
/* ============================================================ */

/* Returns the most recently completed XRGB8888 framebuffer and its size.
 * On width/height change, returns the new dimensions and the buffer is
 * sized for them. Valid until the next frame completes. */
const uint32_t *ymir_bridge_get_framebuffer(YmirInstance *inst,
                                            int32_t *out_width,
                                            int32_t *out_height);

/* Set the Virtua Gun reference framebuffer size — the dart overlay
 * scales touch coords into this space. */
void ymir_bridge_set_virtua_gun_fb_size(YmirInstance *inst,
                                        int32_t width, int32_t height);

/* ============================================================ */
/*  peripherals                                                 */
/* ============================================================ */

/* Switch a SMPC port (1 or 2) to the given peripheral type.
 * Disconnects any existing peripheral on that port. */
void ymir_bridge_set_peripheral_type(YmirInstance *inst,
                                     int32_t port,
                                     YmirPeripheralType type);

/* Returns the active peripheral type for a port. */
YmirPeripheralType ymir_bridge_get_peripheral_type(YmirInstance *inst, int32_t port);

/* Set the pressed state of a single Saturn pad button on a port.
 * The bridge inverts the bit (Ymir reports 1=released, 0=pressed). */
void ymir_bridge_set_pad_button(YmirInstance *inst,
                                int32_t port,
                                int32_t button_index,
                                int32_t pressed);

/* Feed Virtua Gun input. (x, y) are framebuffer pixels (NOT host pixels),
 * clamped to [1, fb_w-1] x [1, fb_h-1] on the dart side. Pass trigger=1
 * for a press, 0 for release. start mirrors the Saturn Start button. */
void ymir_bridge_set_virtua_gun_input(YmirInstance *inst, int32_t port,
                                      int32_t x, int32_t y,
                                      int32_t trigger_pressed,
                                      int32_t start_pressed);

/* Feed Virtua Gun input including reload. Reload is what a game reads as
 * "trigger pulled while aiming off-screen"; on a touch screen there is no
 * off-screen to aim at, so the overlay raises it explicitly.
 *
 * Deliberately a new symbol rather than extra parameters on the call above:
 * a prebuilt core that predates this change then fails the CI symbol check
 * loudly instead of being handed an argument it does not read. */
void ymir_bridge_set_virtua_gun_state(YmirInstance *inst, int32_t port,
                                      int32_t x, int32_t y,
                                      int32_t trigger_pressed,
                                      int32_t start_pressed,
                                      int32_t reload_pressed);

/* Shuttle Mouse relative movement. Deltas accumulate and are delivered to
 * the game across as many peripheral reads as they need -- the report can
 * only carry -256..255 per read, and clamping a fast swipe would land the
 * pointer short of where it was aimed. */
void ymir_bridge_set_mouse_motion(YmirInstance *inst, int32_t port,
                                  int32_t dx, int32_t dy);

/* Shuttle Mouse button state (1 = pressed). */
void ymir_bridge_set_mouse_button(YmirInstance *inst, int32_t port,
                                  YmirMouseButton button, int32_t pressed);

/* Analog pad axis: left_x and left_y in [0, 255] (128 = center). */
void ymir_bridge_set_analog_axis(YmirInstance *inst, int32_t port,
                                 int32_t left_x, int32_t left_y,
                                 int32_t right_x, int32_t right_y);

/* ============================================================ */
/*  save state                                                  */
/* ============================================================ */

/* Atomic swap: save the current state to `<path>.cur`, then load
 * `<path>`. On success, delete `<path>.cur`. Returns YMIR_OK or
 * YMIR_ERR_SNAPSHOT_TIMEOUT. */
int32_t ymir_bridge_swap_state(YmirInstance *inst, const char *path);

/* Save the current state to path. */
int32_t ymir_bridge_save_state(YmirInstance *inst, const char *path);

/* Load state from path. */
int32_t ymir_bridge_load_state(YmirInstance *inst, const char *path);

/* ---- RTC ---- */
/* Set the SMPC real-time clock to host time (optionally offset by
 * `offsetSeconds`). Called before LoadIPL so the BIOS Set Clock
 * wizard shows the device's current date+time. */
void ymir_bridge_set_rtc_to_host(YmirInstance *inst, int64_t offsetSeconds);

/* Set the SMPC persistent-data file path. ymir-core reads this file
 * automatically when SMPC boots; if the file exists and contains a
 * valid 25-byte SMPC state, the BIOS skips the Set Clock / Set
 * Language wizard. Called BEFORE LoadIPL. */
void ymir_bridge_set_persistent_smpc_path(YmirInstance *inst, const char *path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YMIR_BRIDGE_H */