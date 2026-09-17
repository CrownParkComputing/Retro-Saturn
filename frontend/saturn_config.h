/*
 * Retro-Saturn — persisted settings.
 *
 * Plain key=value text, the same shape the rest of this estate uses: small,
 * fixable in a text editor when something will not start, and it survives an
 * app update with no migration code.
 *
 * What is here is what a Saturn actually needs and a DOS machine does not: a
 * BIOS image, two controller ports each with its own peripheral, and a folder
 * of discs rather than a folder of folders.
 */
#ifndef SATURN_CONFIG_H
#define SATURN_CONFIG_H

#include <string>

namespace saturn {

/* Mirrors YmirPeripheralType so the UI can hold one without dragging the
 * bridge header into every file. Values match; the bridge is the authority. */
enum class Peripheral {
    None = 0,
    ControlPad = 1,
    AnalogPad = 2,
    ArcadeRacer = 3,
    MissionStick = 4,
    VirtuaGun = 5,
    ShuttleMouse = 6,
};

const char *peripheral_name(Peripheral p);

struct Settings {
    /* ---- the machine ----
     *
     * Defaults are the real hardware's behaviour, not the fastest or the
     * prettiest. Someone who wants a Saturn should get a Saturn, and every
     * departure from that should be a choice they made.
     */

    /* Work out NTSC or PAL from the disc. Off means the value below is used,
     * which is what a disc that lies about its region needs. */
    bool region_auto = true;
    /* 0 = NTSC, 1 = PAL. Ignored while region_auto is on. */
    int  video_standard = 0;

    /* The SH2 cache. Accurate and slower; a handful of titles need it and
     * most do not notice. */
    bool sh2_cache = false;

    /* Percent. 100 is the real clock -- and the only value that is
     * guaranteed correct, because games written for a 28MHz machine can and
     * do break when it is faster. */
    int  sh2_clock = 100;

    /* The renderers run on their own threads by default: this is a
     * four-core-minimum emulator and the VDPs are most of its work. */
    bool threaded_vdp1 = true;
    bool threaded_vdp2 = true;
    bool threaded_deinterlace = true;

    /* 0 = nearest, 1 = linear. The SCSP interpolates linearly, so linear is
     * the accurate one despite sounding like the "enhanced" option. */
    int  audio_interpolation = 1;

    /* Silence, kept across restarts. Worth having as a setting rather than a
     * transient toggle: somebody who plays with the sound off wants it off
     * tomorrow too, and somebody who thinks the app is broken can see the
     * switch and rule it out. */
    bool audio_muted = false;

    /* 2 is the real drive's speed. Faster cuts loading, and a few titles that
     * stream from the disc in time with the music notice. */
    int  cd_read_speed = 2;

    /* Low-level CD block emulation: slower, and right for the discs the
     * high-level path cannot read. */
    bool cdblock_lle = false;

    /* The clock. 0 follows the host's, which is why the Saturn knows what
     * time it is without ever being told; 1 emulates the RTC from the bus
     * clock, which is what a run that must be reproducible needs. */
    int  rtc_mode = 0;

    /* ---- how the picture reaches the screen ----
     *
     * The same treatment psx-core applies, and for the same reason. A Saturn
     * draws 320x224 and a window is almost never a whole multiple of that;
     * blitting hard pixels at a fractional scale makes some rows one pixel
     * tall and others two, which reads as faint horizontal banding across the
     * whole picture. It is the single most common way an emulator looks worse
     * than the machine it is emulating.
     *
     *   0 Auto   hard pixels when the scale divides evenly, smooth when it
     *            does not -- sharp where sharp is correct, and no banding
     *            where it is not
     *   1 Sharp  always nearest, banding and all, for anyone who wants it
     *   2 Smooth always bilinear
     */
    int  scaling = 0;

    /* Snap the picture to a whole multiple of itself and accept the border.
     * Every pixel the same size, which is the only way to be certain. */
    bool integer_scale = false;

    /* 0 = 4:3, the shape a Saturn was drawn for. 1 = fill the window, for a
     * display where the border matters more than the geometry. */
    int  aspect = 0;

    /* Crosshair size, as a percentage of a sensible default. A light gun
     * pointer has to be found on a busy screen in a hurry, and how big that
     * needs to be depends on the screen and the eyes, so it is a setting. */
    int  crosshair = 250;

    /* ---- the ports ----
     *
     * Two of them, because the console has two, and they are not a setting
     * buried in a page -- they are on the front of the machine.
     */
    Peripheral port1 = Peripheral::ControlPad;
    Peripheral port2 = Peripheral::None;

    bool operator==(const Settings &o) const {
        return region_auto == o.region_auto &&
               video_standard == o.video_standard && sh2_cache == o.sh2_cache &&
               sh2_clock == o.sh2_clock && threaded_vdp1 == o.threaded_vdp1 &&
               threaded_vdp2 == o.threaded_vdp2 &&
               threaded_deinterlace == o.threaded_deinterlace &&
               audio_interpolation == o.audio_interpolation &&
               audio_muted == o.audio_muted &&
               cd_read_speed == o.cd_read_speed && cdblock_lle == o.cdblock_lle && rtc_mode == o.rtc_mode && scaling == o.scaling &&
               integer_scale == o.integer_scale && aspect == o.aspect &&
               crosshair == o.crosshair &&
               port1 == o.port1 && port2 == o.port2;
    }
};

struct AppConfig {
    /* Where discs are kept. One folder of disc images, not a folder of game
     * folders: a Saturn game IS a disc. */
    std::string disc_root;

    /* The Saturn will not start without one, and we do not ship one -- it is
     * Sega's. Held as a path so the wizard can check it is still there. */
    std::string bios_path;

    /*
     * Where game saves live -- the Saturn's battery RAM and our save states.
     *
     * Deliberately NOT inside the app's own storage. On Android that is wiped
     * by an uninstall and is not somewhere a person can back up, so a
     * reinstall would take every save file with it. This defaults to a folder
     * beside the discs, which is a place the user chose and granted, which
     * survives an update or a reinstall, and which they can copy off the
     * device themselves.
     *
     * Empty means "work it out from disc_root", so an existing config picks
     * up the default without a migration step.
     */
    std::string saves_dir;

    bool wizard_done = false;
    Settings machine;
};

bool load_app_config(const std::string &path, AppConfig &out);
bool save_app_config(const std::string &path, const AppConfig &cfg);

} /* namespace saturn */

#endif /* SATURN_CONFIG_H */
