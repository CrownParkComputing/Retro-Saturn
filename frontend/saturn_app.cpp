/*
 * Retro-Saturn — a native front end for the Ymir Saturn core.
 *
 * SDL3 for the window, Dear ImGui for the interface, and the emulator behind
 * the plain-C bridge in core/retro/bridge. The frontend owns the window and
 * the main thread; the bridge owns a worker thread that runs the Saturn and
 * hands back a finished framebuffer. Nothing here calls into ymir-core.
 *
 * WHY IT DOES NOT LOOK LIKE THE DOS ONE
 * -------------------------------------
 * Apple rejected this estate under guideline 4.3 for being one application
 * shipped several times, so "take the DOS front end and change the colours" is
 * the one thing that must not happen. The shape here comes from the machine
 * instead:
 *
 *   - A Saturn game is a DISC. The disc in the drive is the most important
 *     thing on screen, so it is the top of the window rather than a row in a
 *     list. Multi-disc games are one shelf entry with a disc selector, because
 *     that is what they are.
 *   - A Saturn has TWO CONTROLLER PORTS on the front of the console, and which
 *     peripheral is in them changes how games play -- a light gun, a wheel, a
 *     mouse. They live along the bottom of the window, always visible, like
 *     the sockets they represent.
 *   - Navigation is a horizontal row, not a vertical rail.
 *
 * The result reads as a console, which the DOS app deliberately does not.
 */
#include "saturn_config.h"
#include "saturn_library.h"
#include "saturn_setup.h"

/* Stamped in by the build script; sensible if it was not. */
#ifndef SATURN_CORE_VERSION
#  define SATURN_CORE_VERSION "unknown"
#endif
#ifndef SATURN_CORE_DESC
#  define SATURN_CORE_DESC "unknown"
#endif
#ifndef SATURN_CORE_DATE
#  define SATURN_CORE_DATE "unknown"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

extern "C" {
#include "ymir_bridge.h"
}

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

void TextDim(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.70f, 1.0f));
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void TextDimWrapped(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.70f, 1.0f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

std::string base_name(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool path_is_file(const std::string &p)
{
    SDL_PathInfo info;
    return !p.empty() && SDL_GetPathInfo(p.c_str(), &info) &&
           info.type == SDL_PATHTYPE_FILE;
}

/*
 * The Saturn's own palette, not a theme.
 *
 * Deep blue-grey with a warm amber accent: the colours of the console's box
 * and the CD player screen it boots to. Chosen so that a screenshot of this
 * app and a screenshot of the DOS one could not be mistaken for each other.
 */
void apply_style()
{
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.FramePadding = ImVec2(10.0f, 7.0f);
    s.ItemSpacing = ImVec2(9.0f, 7.0f);
    s.WindowPadding = ImVec2(14.0f, 12.0f);
    s.ScrollbarSize = 14.0f;

    ImVec4 *c = s.Colors;
    const ImVec4 ink      = ImVec4(0.055f, 0.063f, 0.090f, 1.00f);
    const ImVec4 panel    = ImVec4(0.094f, 0.106f, 0.145f, 1.00f);
    const ImVec4 raised   = ImVec4(0.137f, 0.153f, 0.204f, 1.00f);
    const ImVec4 amber    = ImVec4(0.949f, 0.639f, 0.157f, 1.00f);
    const ImVec4 amberDim = ImVec4(0.949f, 0.639f, 0.157f, 0.35f);

    c[ImGuiCol_WindowBg]        = ink;
    c[ImGuiCol_ChildBg]         = panel;
    c[ImGuiCol_PopupBg]         = panel;
    c[ImGuiCol_Border]          = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBg]         = raised;
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.20f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.22f, 0.24f, 0.32f, 1.00f);
    c[ImGuiCol_Button]          = raised;
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.20f, 0.23f, 0.31f, 1.00f);
    c[ImGuiCol_ButtonActive]    = amber;
    c[ImGuiCol_Header]          = amberDim;
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.23f, 0.31f, 1.00f);
    c[ImGuiCol_HeaderActive]    = amber;
    c[ImGuiCol_Separator]       = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_Text]            = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.52f, 0.55f, 0.62f, 1.00f);
    c[ImGuiCol_CheckMark]       = amber;
    c[ImGuiCol_SliderGrab]      = amber;
    c[ImGuiCol_SliderGrabActive]= ImVec4(1.00f, 0.72f, 0.26f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = panel;
}

/*
 * A PNG, as a texture.
 *
 * SDL3 has no image decoder of its own and this app links no SDL_image, but
 * the core already vendors stb_image for its own use -- so the wordmark costs
 * one header rather than a dependency. Null on any failure, which every caller
 * here treats as "draw the name instead".
 */
SDL_Texture *load_png(SDL_Renderer *ren, const char *path, int *out_w, int *out_h)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc *px = stbi_load(path, &w, &h, &comp, 4);
    if (!px) return nullptr;

    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, nullptr, px, w * 4);
        /* Drawn smaller than it was authored, so filtered -- the opposite of
         * the emulator's framebuffer, which is hard pixels. */
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
    }
    stbi_image_free(px);
    return t;
}

/* Which of the console's faces is showing. Horizontal, and short: a Saturn
 * has a disc, a machine and two ports, and that really is all of it. */
enum class Face { Discs, Console, About };

/* The keyboard, mapped to a Control Pad. A desktop has no Saturn pad, and
 * these are the bindings every Saturn emulator has used since the 90s. */
struct KeyBind { SDL_Scancode key; YmirButton button; };
const KeyBind kKeyMap[] = {
    { SDL_SCANCODE_UP,     YMIR_BUTTON_UP    },
    { SDL_SCANCODE_DOWN,   YMIR_BUTTON_DOWN  },
    { SDL_SCANCODE_LEFT,   YMIR_BUTTON_LEFT  },
    { SDL_SCANCODE_RIGHT,  YMIR_BUTTON_RIGHT },
    { SDL_SCANCODE_RETURN, YMIR_BUTTON_START },
    { SDL_SCANCODE_Z,      YMIR_BUTTON_A     },
    { SDL_SCANCODE_X,      YMIR_BUTTON_B     },
    { SDL_SCANCODE_C,      YMIR_BUTTON_C     },
    { SDL_SCANCODE_A,      YMIR_BUTTON_X     },
    { SDL_SCANCODE_S,      YMIR_BUTTON_Y     },
    { SDL_SCANCODE_D,      YMIR_BUTTON_Z     },
    { SDL_SCANCODE_Q,      YMIR_BUTTON_L     },
    { SDL_SCANCODE_W,      YMIR_BUTTON_R     },
};

/*
 * A real pad, mapped to the Saturn's.
 *
 * The Saturn has six face buttons in two rows -- A B C along the bottom, X Y Z
 * along the top -- and an L and R. A modern pad has four face buttons and four
 * shoulders, so the two rows have to borrow: the four faces are A B X Y, and
 * the bumpers carry C and Z. That is the arrangement every Saturn emulator has
 * settled on, and it puts the six-button fighting layout under the six buttons
 * a thumb can reach.
 */
struct PadBind { SDL_GamepadButton pad; YmirButton button; };
const PadBind kPadMap[] = {
    { SDL_GAMEPAD_BUTTON_DPAD_UP,        YMIR_BUTTON_UP    },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN,      YMIR_BUTTON_DOWN  },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT,      YMIR_BUTTON_LEFT  },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     YMIR_BUTTON_RIGHT },
    { SDL_GAMEPAD_BUTTON_START,          YMIR_BUTTON_START },
    { SDL_GAMEPAD_BUTTON_SOUTH,          YMIR_BUTTON_A     },
    { SDL_GAMEPAD_BUTTON_EAST,           YMIR_BUTTON_B     },
    { SDL_GAMEPAD_BUTTON_WEST,           YMIR_BUTTON_X     },
    { SDL_GAMEPAD_BUTTON_NORTH,          YMIR_BUTTON_Y     },
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, YMIR_BUTTON_C     },
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  YMIR_BUTTON_Z     },
};

/* The triggers are axes, not buttons, so they are read rather than bound.
 * Half travel counts as pressed: an analogue trigger standing in for a digital
 * shoulder should fire where a thumb expects it to, not at the very bottom. */
const Sint16 kTriggerOn = 16384;

} /* namespace */

/*
 * A disc on the command line starts it.
 *
 * Every desktop application opens the file it is given, and a file manager
 * expects that too -- double-clicking a .chd should run the game, not show a
 * shelf with the game on it. It is also the only way to start this thing
 * without a mouse, which is what made it testable.
 */
int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    if (!SDL_CreateWindowAndRenderer("Retro-Saturn", 1280, 800,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /*
     * Gamepad navigation is deliberately OFF.
     *
     * With a pad plugged in it drives the interface as well as the Saturn, and
     * a stick that rests a little off centre -- which is most of them -- walks
     * the focus and then activates whatever it lands on. The first-run wizard
     * vanished before it could be read, having pressed its own Skip button,
     * and the config came back saying the setup had been completed. Nothing on
     * screen suggested why.
     *
     * The pad's job here is to play the Saturn. The menus have a mouse and a
     * keyboard.
     */
    ImGui::GetIO().IniFilename = nullptr;   /* no imgui.ini beside the binary */
    apply_style();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);
    /*
     * The interface takes no gamepads at all.
     *
     * Turning off NavEnableGamepad is not enough: the SDL3 backend defaults to
     * AutoFirst, which OPENS the first pad itself and feeds it to ImGui. Two
     * consequences, both seen here. The interface navigated and activated
     * itself -- the first-run wizard pressed its own Skip button and was gone
     * before it could be read -- and the pad was already claimed, so the
     * emulator's own handler never saw it.
     *
     * Manual with no gamepads is the documented way to say "none". The pad is
     * opened below, by this app, for the Saturn.
     */
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    /* ---- where settings live ---- */
    std::string cfg_dir;
    if (char *pref = SDL_GetPrefPath("CrownParkComputing", "Retro-Saturn")) {
        cfg_dir = pref;
        SDL_free(pref);
    }
    const std::string cfg_path = cfg_dir + "retrosaturn.cfg";

    saturn::AppConfig cfg;
    if (cfg.disc_root.empty()) {
        /* A sensible first guess, created on demand rather than at startup:
         * an empty folder the user never asked for is litter. */
        if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
            cfg.disc_root = std::string(home) + "Saturn";
    }
    saturn::load_app_config(cfg_path, cfg);

    std::vector<saturn::Game> games = saturn::scan_discs(cfg.disc_root);
    auto rescan = [&] { games = saturn::scan_discs(cfg.disc_root); };

    /* ---- the machine ---- */
    YmirInstance *ymir = ymir_bridge_create();
    if (!ymir) {
        SDL_Log("could not create the Saturn instance");
        return 1;
    }

    bool bios_loaded = false;
    auto apply_options = [&] {
        const saturn::Settings &s = cfg.machine;
        ymir_bridge_set_core_option(ymir, YMIR_OPT_AUTODETECT_REGION, s.region_auto);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_VIDEO_STANDARD, s.video_standard);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_EMULATE_SH2_CACHE, s.sh2_cache);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_SH2_OVERCLOCK, s.sh2_clock);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_THREADED_VDP1, s.threaded_vdp1);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_THREADED_VDP2, s.threaded_vdp2);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_THREADED_DEINTERLACE,
                                    s.threaded_deinterlace);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_AUDIO_INTERPOLATION,
                                    s.audio_interpolation);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_CD_READ_SPEED, s.cd_read_speed);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_CDBLOCK_LLE, s.cdblock_lle);
        ymir_bridge_set_core_option(ymir, YMIR_OPT_RTC_MODE, s.rtc_mode);
    };
    auto apply_ports = [&] {
        ymir_bridge_set_peripheral_type(ymir, 1, (YmirPeripheralType)cfg.machine.port1);
        ymir_bridge_set_peripheral_type(ymir, 2, (YmirPeripheralType)cfg.machine.port2);
    };
    auto load_bios = [&] {
        bios_loaded = path_is_file(cfg.bios_path) &&
                      ymir_bridge_load_bios(ymir, cfg.bios_path.c_str()) == YMIR_OK;
        return bios_loaded;
    };

    /*
     * The Saturn's own persistent memory: its clock and its language.
     *
     * Without this the BIOS shows its "Set Language / Set Time" screen on
     * EVERY boot, because from the machine's point of view it has never been
     * switched on before. A real Saturn keeps those in battery-backed SMPC
     * memory, and this is that battery.
     *
     * Loaded before the BIOS, saved when the app closes and after each
     * session, so a language chosen once stays chosen.
     */
    const std::string smpc_path = cfg_dir + "smpc.bin";
    ymir_bridge_set_persistent_smpc_path(ymir, smpc_path.c_str());
    ymir_bridge_load_smpc_state(ymir, smpc_path.c_str());

    apply_options();
    apply_ports();
    load_bios();

    /* ---- what is in the drive ---- */
    std::string loaded_title;     /* empty = no disc */
    std::string loaded_path;
    int  loaded_disc_index = 0;
    int  loaded_game_index = -1;
    std::string message;          /* one line, shown under the drive */

    auto insert_disc = [&](int gameIndex, int discIndex) {
        if (gameIndex < 0 || gameIndex >= (int)games.size()) return;
        const saturn::Game &g = games[gameIndex];
        const saturn::Disc *d = g.disc((size_t)discIndex);
        if (!d) return;
        if (!bios_loaded) {
            message = "No BIOS loaded. The Saturn cannot start without one.";
            return;
        }
        const int32_t rc = ymir_bridge_load_disc(ymir, d->path.c_str());
        if (rc != YMIR_OK) {
            message = "Could not read " + d->file;
            return;
        }
        /*
         * Reconnect the peripherals AFTER the disc.
         *
         * Loading a BIOS or a disc resets the machine, and a reset takes the
         * SMPC's ports with it -- so a Control Pad connected at startup is
         * gone by the time the game is asking for input. It looks exactly like
         * a broken key map: the picture is fine, the sound is fine, and
         * nothing you press does anything.
         */
        apply_ports();

        loaded_title = g.title;
        loaded_path = d->path;
        loaded_disc_index = discIndex;
        loaded_game_index = gameIndex;
        message = g.multi()
                    ? ("Disc " + std::to_string(d->number ? d->number : discIndex + 1) +
                       " of " + std::to_string(g.discs.size()))
                    : std::string();
    };

    /* ---- whatever is plugged in ----
     *
     * Opened by instance id rather than by index, and re-opened on
     * SDL_EVENT_GAMEPAD_ADDED, because a pad plugged in after the app started
     * is the common case on a desktop and "restart it and it will work" is not
     * an answer. Port 1 gets the first pad; a second pad drives port 2. */
    std::vector<SDL_Gamepad *> pads;
    auto open_pads = [&] {
        for (SDL_Gamepad *g : pads) if (g) SDL_CloseGamepad(g);
        pads.clear();
        int count = 0;
        if (SDL_JoystickID *ids = SDL_GetGamepads(&count)) {
            for (int i = 0; i < count; ++i)
                if (SDL_Gamepad *g = SDL_OpenGamepad(ids[i])) pads.push_back(g);
            SDL_free(ids);
        }
    };
    /* Which Saturn port a pad drives: the first pad is port 1, the second
     * port 2, and anything after that is ignored -- the console has two
     * sockets. */
    auto port_of = [&](SDL_JoystickID which) -> int {
        for (size_t i = 0; i < pads.size() && i < 2; ++i)
            if (pads[i] && SDL_GetGamepadID(pads[i]) == which) return (int)i + 1;
        return 0;
    };
    open_pads();

    /* Trigger state, so a crossing of the threshold is sent once rather than
     * every frame the trigger is held. */
    bool trigger_held[2][2] = {{false, false}, {false, false}};

    /* ---- the wordmark ----
     *
     * Loaded once, from beside the binary. A missing one is not an error: the
     * app draws its name as text and carries on, because an asset that failed
     * to package should make it look plainer, never stop it starting. */
    SDL_Texture *logo = nullptr;
    int logo_w = 0, logo_h = 0;
    if (const char *base = SDL_GetBasePath()) {
        logo = load_png(ren, (std::string(base) + "assets/wordmark.png").c_str(),
                        &logo_w, &logo_h);
    }

    /* ---- the picture ---- */
    SDL_Texture *frame = nullptr;
    int frame_w = 0, frame_h = 0;

    /* A disc named on the command line goes straight in, and straight on. */
    if (argc > 1 && argv[1] && argv[1][0] != '-') {
        const std::string want = argv[1];
        rescan();
        for (size_t i = 0; i < games.size() && loaded_game_index < 0; ++i)
            for (size_t d = 0; d < games[i].discs.size(); ++d)
                if (games[i].discs[d].path == want ||
                    games[i].discs[d].file == base_name(want)) {
                    insert_disc((int)i, (int)d);
                    break;
                }
        if (loaded_game_index < 0) {
            /* Not in the shelf -- a disc from anywhere else on the system.
             * Take it on its own terms rather than refusing it. */
            std::string title;
            saturn::Game g;
            saturn::Disc d;
            d.path = want;
            d.file = base_name(want);
            d.number = saturn::disc_number_of(d.file, title);
            g.title = title.empty() ? d.file : title;
            g.discs.push_back(d);
            games.insert(games.begin(), g);
            insert_disc(0, 0);
        }
    }

    Face face = Face::Discs;
    /* The wizard runs until it is finished once, and can be asked for again
     * from Console. A disc on the command line skips it: somebody who
     * double-clicked a game has answered the only question it asks. */
    bool wizard = !cfg.wizard_done && loaded_game_index < 0;
    int  wstep = 0;
    bool running_view = loaded_game_index >= 0;   /* given a disc: play it */
    bool show_pause = false;
    char search[96] = {0};
    bool quit = false;

    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) quit = true;

            if (running_view && !show_pause &&
                (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP)) {
                const bool down = ev.type == SDL_EVENT_KEY_DOWN;
                if (down && ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                    show_pause = true;
                } else {
                    for (const KeyBind &b : kKeyMap)
                        if (b.key == ev.key.scancode)
                            ymir_bridge_set_pad_button(ymir, 1, b.button, down);
                }
            } else if (running_view && show_pause && ev.type == SDL_EVENT_KEY_DOWN &&
                       ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                show_pause = false;
            }

            /* ---- a real pad ---- */
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED ||
                ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
                open_pads();
            }
            else if (running_view && !show_pause &&
                     (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                      ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP)) {
                const int port = port_of(ev.gbutton.which);
                if (port) {
                    const bool down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                    /* Back opens the menu, because a handheld has no Escape
                     * key and a pad in your hands should not need one. */
                    if (down && ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                        show_pause = true;
                    } else {
                        for (const PadBind &b : kPadMap)
                            if (b.pad == ev.gbutton.button)
                                ymir_bridge_set_pad_button(ymir, port, b.button, down);
                    }
                }
            }
            else if (running_view && !show_pause &&
                     ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                const int port = port_of(ev.gaxis.which);
                if (port) {
                    const int slot = port - 1;
                    if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                        ev.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                        const bool left = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                        const bool now = ev.gaxis.value > kTriggerOn;
                        bool &was = trigger_held[slot][left ? 0 : 1];
                        if (now != was) {
                            was = now;
                            ymir_bridge_set_pad_button(
                                ymir, port, left ? YMIR_BUTTON_L : YMIR_BUTTON_R, now);
                        }
                    }
                }
            }
        }

        /*
         * The sticks, read rather than evented.
         *
         * An axis event arrives only when the value changes, and a stick held
         * off-centre produces none -- so a direction held would be sent once
         * and then contradicted by nothing. Sampling the state each frame is
         * what a pad actually is.
         *
         * The left stick doubles as the d-pad. Most Saturn games predate the
         * 3D Control Pad and read only the digital directions, so a stick that
         * does nothing in them is a pad that appears broken.
         */
        if (running_view && !show_pause) {
            for (size_t i = 0; i < pads.size() && i < 2; ++i) {
                SDL_Gamepad *g = pads[i];
                if (!g) continue;
                const int port = (int)i + 1;
                const Sint16 lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
                const Sint16 ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
                const Sint16 rx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
                const Sint16 ry = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);

                /* Generous, because a worn stick rests off zero and a pad that
                 * walks the menu on its own is worse than one that needs a
                 * firmer push. */
                const Sint16 dead = 12000;
                static bool dirHeld[2][4] = {};
                const bool want[4] = { ly < -dead, ly > dead, lx < -dead, lx > dead };
                const YmirButton dirs[4] = { YMIR_BUTTON_UP, YMIR_BUTTON_DOWN,
                                             YMIR_BUTTON_LEFT, YMIR_BUTTON_RIGHT };
                for (int d = 0; d < 4; ++d) {
                    if (want[d] != dirHeld[i][d]) {
                        dirHeld[i][d] = want[d];
                        ymir_bridge_set_pad_button(ymir, port, dirs[d], want[d]);
                    }
                }

                /* And as an analogue stick, for the ports that are one. The
                 * bridge wants 0..255 with 128 at rest. */
                const auto to255 = [](Sint16 v) {
                    return (int32_t)(((int)v + 32768) * 255 / 65535);
                };
                const saturn::Peripheral p = (port == 1) ? cfg.machine.port1
                                                         : cfg.machine.port2;
                if (p == saturn::Peripheral::AnalogPad ||
                    p == saturn::Peripheral::ArcadeRacer ||
                    p == saturn::Peripheral::MissionStick) {
                    ymir_bridge_set_analog_axis(ymir, port, to255(lx), to255(ly),
                                                to255(rx), to255(ry));
                }
            }
        }

        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(win, &win_w, &win_h);

        /* ---- pull the latest frame ---- */
        if (loaded_game_index >= 0) {
            int w = 0, h = 0;
            const uint32_t *pix = ymir_bridge_get_framebuffer(ymir, &w, &h);
            if (pix && w > 0 && h > 0) {
                if (!frame || w != frame_w || h != frame_h) {
                    if (frame) SDL_DestroyTexture(frame);
                    /*
                     * XBGR, not XRGB, whatever the bridge header says.
                     *
                     * ymir-core's Color888 is a union whose bitfields run
                     * r:8, g:8, b:8 -- and bitfields fill from the least
                     * significant end, so on a little-endian machine red is
                     * the LOW byte: 0x00BBGGRR. SDL's XRGB8888 is 0x00RRGGBB.
                     * Asking for that swaps red and blue, which is why the
                     * demo's blue came out orange.
                     */
                    frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XBGR8888,
                                              SDL_TEXTUREACCESS_STREAMING, w, h);
                    /* Nearest: a Saturn's 320x224 is hard pixels, and a
                     * filtered one is a blur of them. */
                    if (frame) SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST);
                    frame_w = w; frame_h = h;
                }
                if (frame) SDL_UpdateTexture(frame, nullptr, pix, w * 4);
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        /* ================= first run ================= */
        if (wizard) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
            ImGui::Begin("##setup", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            static const char *kStep[] = { "Welcome", "The BIOS", "Your discs",
                                           "Something to run", "Ready" };
            const int kSteps = (int)SDL_arraysize(kStep);
            if (wstep < 0) wstep = 0;
            if (wstep >= kSteps) wstep = kSteps - 1;
            const float fs2 = ImGui::GetFontSize();

            ImGui::Text("Retro-Saturn  -  step %d of %d: %s", wstep + 1, kSteps,
                        kStep[wstep]);
            ImGui::Separator();
            ImGui::BeginChild("##wbody",
                              ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.6f));
            ImGui::PushTextWrapPos(0.0f);

            bool can_advance = true;

            if (wstep == 0) {
                ImGui::TextWrapped("This is a Sega Saturn. It runs the discs you "
                                   "already own, as disc images -- .cue, .chd, .iso "
                                   "or .ccd files.");
                ImGui::Spacing();
                ImGui::TextWrapped("Two things are needed before it can start, and "
                                   "the next two screens are about them: a BIOS, and "
                                   "somewhere to keep your discs.");
                ImGui::Spacing();
                TextDimWrapped("The emulation is Ymir's work, under the GPL. This app "
                               "supplies no BIOS and no games.");
            }

            else if (wstep == 1) {
                ImGui::TextWrapped("The Saturn will not start without its BIOS, and "
                                   "this app cannot give you one -- it is Sega's, and "
                                   "copyrighted.");
                ImGui::Spacing();
                ImGui::TextWrapped("Dump it from a console you own. It is a 512 KB "
                                   "file, usually called saturn_bios.bin.");
                ImGui::Spacing();

                static char bpath[1024];
                static bool bprimed = false;
                if (!bprimed) { SDL_strlcpy(bpath, cfg.bios_path.c_str(), sizeof bpath);
                                bprimed = true; }
                ImGui::BeginDisabled(saturn::pick_in_progress());
                if (ImGui::Button("Choose the BIOS file...")) saturn::begin_pick_file();
                ImGui::EndDisabled();
                if (saturn::pick_in_progress()) {
                    ImGui::SameLine();
                    TextDim("choosing...");
                }
                {   /* The answer arrives whenever the user is finished. */
                    std::string got;
                    if (saturn::take_pick(got)) {
                        cfg.bios_path = got;
                        SDL_strlcpy(bpath, got.c_str(), sizeof bpath);
                        load_bios();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.8f);
                if (ImGui::InputText("##bp", bpath, sizeof bpath)) {
                    cfg.bios_path = bpath;
                    load_bios();
                    saturn::save_app_config(cfg_path, cfg);
                }

                ImGui::Spacing();
                if (bios_loaded) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                    ImGui::TextUnformatted("Loaded. That is the hard part done.");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                    ImGui::TextUnformatted(cfg.bios_path.empty()
                                           ? "No BIOS chosen yet."
                                           : "That file could not be read as a BIOS.");
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                    TextDimWrapped("You can carry on without one and come back later "
                                   "-- you will be able to look around, but nothing "
                                   "will boot.");
                }
            }

            else if (wstep == 2) {
                ImGui::TextWrapped("Where do you keep your disc images?");
                ImGui::Spacing();
#if defined(__ANDROID__)
                /*
                 * Android stopped handing out storage.
                 *
                 * There is no "give this app your files" any more: the system
                 * grants ONE directory at a time, the one chosen in its own
                 * picker, and nothing else. Saying so here is the difference
                 * between a user who picks a folder and one who goes looking
                 * for a permission switch that no longer exists.
                 */
                TextDimWrapped("Android grants one folder at a time -- the one you "
                               "pick, and nothing around it. This app never asks for "
                               "access to all your files.");
                ImGui::Spacing();
                ImGui::TextWrapped("The simplest choice is the app's own folder, "
                                   "which needs no permission at all and which any "
                                   "file manager or a USB cable can reach:");
#else
                TextDimWrapped("One folder of disc images. Not a folder of folders: "
                               "a Saturn game is a disc.");
#endif
                ImGui::Spacing();
                for (const std::string &r : saturn::candidate_disc_roots()) {
                    ImGui::PushID(r.c_str());
                    if (ImGui::RadioButton("##r", cfg.disc_root == r)) {
                        cfg.disc_root = r;
                        SDL_CreateDirectory(r.c_str());
                        rescan();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", r.c_str());
                    ImGui::PopID();
                }

                ImGui::Spacing();
                ImGui::BeginDisabled(saturn::pick_in_progress());
                if (ImGui::Button("Choose another folder...")) saturn::begin_pick_folder();
                ImGui::EndDisabled();
                {
                    std::string got;
                    if (saturn::take_pick(got)) {
                        cfg.disc_root = got;
                        rescan();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                }
                ImGui::Spacing();
                if (!cfg.disc_root.empty()) {
                    SDL_CreateDirectory(cfg.disc_root.c_str());
                    ImGui::Text("Using: %s", cfg.disc_root.c_str());
                    if (ImGui::Button("Look again")) rescan();
                    ImGui::SameLine();
                    if (games.empty()) TextDim("nothing there yet");
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                        ImGui::Text("%zu found", games.size());
                        ImGui::PopStyleColor();
                    }
                } else {
                    can_advance = false;
                    TextDim("Choose a folder to continue.");
                }
            }

            else if (wstep == 3) {
                const std::string demo = saturn::demo_disc_path();
                ImGui::TextWrapped("A demo is included, so there is something to run "
                                   "before you have copied anything across.");
                ImGui::Spacing();
                if (demo.empty()) {
                    TextDimWrapped("This build did not ship with it.");
                } else {
                    ImGui::TextUnformatted(saturn::demo_title());
                    TextDimWrapped("Free Saturn homebrew from the SegaXtreme "
                                   "competition. Not a Sega game, and not ours "
                                   "either -- it is included with permission as "
                                   "something to test with.");
                    ImGui::Spacing();
                    ImGui::BeginDisabled(!bios_loaded);
                    if (ImGui::Button("Run the demo", ImVec2(fs2 * 12.0f, 0))) {
                        /* Finish first: coming back to step four after the
                         * emulator exits, with no sign of why, is worse than
                         * starting again. */
                        cfg.wizard_done = true;
                        saturn::save_app_config(cfg_path, cfg);
                        wizard = false;

                        saturn::Game g;
                        saturn::Disc d;
                        d.path = demo;
                        d.file = "PPPong.cue";
                        g.title = saturn::demo_title();
                        g.discs.push_back(d);
                        games.insert(games.begin(), g);
                        insert_disc(0, 0);
                        running_view = loaded_game_index >= 0;
                    }
                    ImGui::EndDisabled();
                    if (!bios_loaded) {
                        ImGui::Spacing();
                        TextDimWrapped("It needs the BIOS too. Nothing on a Saturn "
                                       "runs without one -- not even a demo.");
                    }
                }
            }

            else {
                ImGui::TextWrapped("Ready.");
                ImGui::Spacing();
                ImGui::Text("BIOS:  %s", bios_loaded ? "loaded" : "not set");
                ImGui::Text("Discs: %s", cfg.disc_root.c_str());
                ImGui::Text("Found: %zu", games.size());
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                TextDimWrapped("The two controller ports are along the bottom of the "
                               "main screen. Change what is plugged into them there "
                               "-- a light gun or a wheel is a different peripheral, "
                               "not a setting.");
                ImGui::Spacing();
                TextDimWrapped("This screen is under Console, as Run setup again.");
            }

            ImGui::PopTextWrapPos();
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::BeginDisabled(wstep == 0);
            if (ImGui::Button("Back")) --wstep;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (wstep < kSteps - 1) {
                ImGui::BeginDisabled(!can_advance);
                if (ImGui::Button("Next")) ++wstep;
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Finish")) {
                    cfg.wizard_done = true;
                    saturn::save_app_config(cfg_path, cfg);
                    wizard = false;
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("   step %d of %d", wstep + 1, kSteps);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - fs2 * 4.0f);
            if (ImGui::SmallButton("Skip")) {
                cfg.wizard_done = true;
                saturn::save_app_config(cfg_path, cfg);
                wizard = false;
            }
            ImGui::End();
        }

        else if (!running_view) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
            ImGui::Begin("##shell", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

            const float cw = ImGui::GetContentRegionAvail().x;
            const float fs = ImGui::GetFontSize();

            /* The wordmark, sized to the window and never wider than a
             * third of it: this is a shelf, not a title screen. */
            if (logo && logo_w > 0) {
                const float want = ImGui::GetFontSize() * 7.0f;
                const float w = std::min(want * (float)logo_w / (float)logo_h,
                                         cw * 0.42f);
                const float h = w * (float)logo_h / (float)logo_w;
                ImGui::Image((ImTextureID)(intptr_t)logo, ImVec2(w, h));
            } else {
                ImGui::TextUnformatted("RETRO-SATURN");
            }
            ImGui::Spacing();

            /* ============ the drive, across the top ============ */
            /* Sized from what is in it, not guessed: three lines of text, a
             * row of buttons, and the padding around them. The first version
             * used a round number of font heights and clipped "Power on"
             * behind a scrollbar. */
            const float drive_h = ImGui::GetTextLineHeightWithSpacing() * 3.0f
                                + ImGui::GetFrameHeightWithSpacing()
                                + ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("##drive", ImVec2(0, drive_h),
                              ImGuiChildFlags_Borders);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.949f, 0.639f, 0.157f, 1.0f));
                ImGui::TextUnformatted(loaded_title.empty() ? "NO DISC"
                                                            : loaded_title.c_str());
                ImGui::PopStyleColor();

                if (!loaded_title.empty())
                    TextDim("%s", base_name(loaded_path).c_str());
                else
                    TextDim("The tray is empty. Choose a disc below.");

                if (!message.empty()) TextDim("%s", message.c_str());

                ImGui::Spacing();
                ImGui::BeginDisabled(loaded_game_index < 0);
                if (ImGui::Button("Power on", ImVec2(fs * 9.0f, 0))) {
                    running_view = true;
                    show_pause = false;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(loaded_game_index < 0);
                if (ImGui::Button("Eject", ImVec2(fs * 6.0f, 0))) {
                    loaded_title.clear(); loaded_path.clear();
                    loaded_game_index = -1; message.clear();
                }
                ImGui::EndDisabled();

                /* The disc selector, only for games that have more than one.
                 * A single-disc game showing "Disc 1 of 1" is noise. */
                if (loaded_game_index >= 0 && games[loaded_game_index].multi()) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("|");
                    const saturn::Game &g = games[loaded_game_index];
                    for (size_t i = 0; i < g.discs.size(); ++i) {
                        ImGui::SameLine();
                        ImGui::PushID((int)i);
                        const bool on = (int)i == loaded_disc_index;
                        if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        char lbl[16];
                        snprintf(lbl, sizeof lbl, "%d",
                                 g.discs[i].number ? g.discs[i].number : (int)i + 1);
                        if (ImGui::Button(lbl)) insert_disc(loaded_game_index, (int)i);
                        if (on) ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();

            /* ============ the faces, horizontally ============ */
            ImGui::Spacing();
            {
                const float bw = (cw - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
                auto tab = [&](const char *label, Face f) {
                    const bool on = (face == f);
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(label, ImVec2(bw, fs * 2.0f))) face = f;
                    if (on) ImGui::PopStyleColor();
                };
                tab("Discs", Face::Discs);
                ImGui::SameLine();
                tab("Console", Face::Console);
                ImGui::SameLine();
                tab("About", Face::About);
            }
            ImGui::Spacing();

            /* ============ the face ============ */
            /* Everything left over, minus the ports along the bottom. */
            const float ports_h = ImGui::GetTextLineHeightWithSpacing()
                                + ImGui::GetFrameHeightWithSpacing()
                                + ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("##face",
                              ImVec2(0, -(ports_h + ImGui::GetStyle().ItemSpacing.y)),
                              ImGuiChildFlags_Borders);

            if (face == Face::Discs) {
                ImGui::SetNextItemWidth(cw * 0.5f);
                ImGui::InputTextWithHint("##find", "Find a game...", search,
                                         sizeof search);
                ImGui::SameLine();
                if (ImGui::Button("Rescan")) rescan();
                ImGui::SameLine();
                TextDim("%zu game%s in %s", games.size(),
                        games.size() == 1 ? "" : "s",
                        cfg.disc_root.empty() ? "(no folder set)"
                                              : cfg.disc_root.c_str());
                ImGui::Separator();

                if (games.empty()) {
                    ImGui::Spacing();
                    TextDimWrapped("No disc images here yet. Put .cue, .chd, .iso or "
                                   ".ccd files in the folder above -- one game per "
                                   "disc, and a game that came on several discs will "
                                   "be grouped back together by its name.");
                } else {
                    ImGui::BeginChild("##list");
                    for (size_t i = 0; i < games.size(); ++i) {
                        const saturn::Game &g = games[i];
                        if (search[0] && !SDL_strcasestr(g.title.c_str(), search))
                            continue;
                        ImGui::PushID((int)i);
                        const bool in_drive = (int)i == loaded_game_index;
                        if (in_drive) ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.949f, 0.639f, 0.157f, 1.0f));
                        if (ImGui::Selectable(g.title.c_str(), in_drive, 0,
                                              ImVec2(0, fs * 1.9f)))
                            insert_disc((int)i, 0);
                        if (in_drive) ImGui::PopStyleColor();
                        ImGui::SameLine(cw * 0.72f);
                        if (g.multi()) TextDim("%zu discs", g.discs.size());
                        else           TextDim("%s", g.discs[0].file.c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
            }

            else if (face == Face::Console) {
                saturn::Settings &s = cfg.machine;
                ImGui::TextUnformatted("BIOS");
                TextDimWrapped("The Saturn will not start without one, and this app "
                               "does not include it -- it is Sega's. Point this at "
                               "the ROM you dumped from your own console.");
                static char bios_buf[1024];
                static bool primed = false;
                if (!primed) {
                    SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                    primed = true;
                }
                ImGui::SetNextItemWidth(cw * 0.7f);
                if (ImGui::InputText("##bios", bios_buf, sizeof bios_buf)) {
                    cfg.bios_path = bios_buf;
                    load_bios();
                    saturn::save_app_config(cfg_path, cfg);
                }
                ImGui::SameLine();
                if (bios_loaded) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                    ImGui::TextUnformatted("loaded");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                    ImGui::TextUnformatted(cfg.bios_path.empty() ? "not set" : "not readable");
                    ImGui::PopStyleColor();
                }

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("Discs folder");
                static char root_buf[1024];
                static bool root_primed = false;
                if (!root_primed) {
                    SDL_strlcpy(root_buf, cfg.disc_root.c_str(), sizeof root_buf);
                    root_primed = true;
                }
                ImGui::SetNextItemWidth(cw * 0.7f);
                if (ImGui::InputText("##root", root_buf, sizeof root_buf)) {
                    cfg.disc_root = root_buf;
                    saturn::save_app_config(cfg_path, cfg);
                    rescan();
                }

                ImGui::Spacing();
                if (ImGui::Button("Run setup again")) { wizard = true; wstep = 0; }
                ImGui::SameLine();
                {
                    const std::string demo = saturn::demo_disc_path();
                    ImGui::BeginDisabled(demo.empty() || !bios_loaded);
                    if (ImGui::Button("Run the demo")) {
                        saturn::Game g;
                        saturn::Disc d;
                        d.path = demo; d.file = "PPPong.cue";
                        g.title = saturn::demo_title();
                        g.discs.push_back(d);
                        games.insert(games.begin(), g);
                        insert_disc(0, 0);
                        running_view = loaded_game_index >= 0;
                    }
                    ImGui::EndDisabled();
                }

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("Machine");
                bool dirty = false;
                dirty |= ImGui::Checkbox("Work out the region from the disc", &s.region_auto);
                if (!s.region_auto) {
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("NTSC", &s.video_standard, 0);
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("PAL", &s.video_standard, 1);
                }
                dirty |= ImGui::Checkbox("Emulate the SH2 cache (accurate, slower)",
                                         &s.sh2_cache);
                ImGui::SetNextItemWidth(cw * 0.5f);
                dirty |= ImGui::SliderInt("Processor speed", &s.sh2_clock, 50, 300, "%d%%");
                TextDimWrapped("100% is the real machine. Games written for it can and "
                               "do break when it is faster.");

                ImGui::Spacing();
                dirty |= ImGui::Checkbox("Threaded VDP1", &s.threaded_vdp1);
                ImGui::SameLine();
                dirty |= ImGui::Checkbox("Threaded VDP2", &s.threaded_vdp2);
                ImGui::SameLine();
                dirty |= ImGui::Checkbox("Threaded deinterlacer", &s.threaded_deinterlace);

                ImGui::Spacing();
                ImGui::SetNextItemWidth(cw * 0.5f);
                dirty |= ImGui::SliderInt("CD read speed", &s.cd_read_speed, 2, 200, "%dx");
                TextDimWrapped("2x is the real drive. Faster cuts loading, and a few "
                               "titles that stream from the disc in time with the "
                               "music will notice.");
                dirty |= ImGui::Checkbox("Low-level CD block (slower, reads more discs)",
                                         &s.cdblock_lle);

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("Sound");
                {
                    /* Linear is what the SCSP does, which makes the accurate
                     * option the one that sounds like an enhancement. */
                    int interp = s.audio_interpolation;
                    dirty |= ImGui::RadioButton("Linear (as the hardware)", &interp, 1);
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("Nearest (harsher, older sound)",
                                                &interp, 0);
                    s.audio_interpolation = interp;
                }

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("Clock");
                {
                    int m = s.rtc_mode;
                    dirty |= ImGui::RadioButton("Follow this device's clock", &m, 0);
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("Emulate the Saturn's own", &m, 1);
                    s.rtc_mode = m;
                }
                TextDimWrapped("Following the device's clock is why the Saturn knows "
                               "the date without ever being told. Emulating its own "
                               "is for a run that has to come out the same twice.");

                if (dirty) {
                    apply_options();
                    saturn::save_app_config(cfg_path, cfg);
                }
            }

            else {
                ImGui::TextUnformatted("Retro-Saturn");
                TextDimWrapped("A Sega Saturn emulator. The emulation is Ymir's work, "
                               "not ours.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Ymir");
                ImGui::Text("version %s", SATURN_CORE_VERSION);
                /* The date the core was last refreshed from upstream, so
                 * "are we behind?" is answerable from inside the app rather
                 * than by reading a submodule pointer. */
                TextDim("%s, refreshed %s", SATURN_CORE_DESC, SATURN_CORE_DATE);
                ImGui::Spacing();
                TextDimWrapped("github.com/StrikerX3/Ymir -- GNU GPL v3 or later. The "
                               "exact source this ships is at "
                               "github.com/CrownParkComputing/ymir.");
                ImGui::Spacing();
                TextDimWrapped("SDL3: zlib. Dear ImGui: MIT.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                TextDimWrapped("No BIOS and no games are included. Both are yours to "
                               "supply, from hardware and discs you own.");
            }
            ImGui::EndChild();

            /* ============ the ports, along the bottom ============ */
            ImGui::Spacing();
            ImGui::BeginChild("##ports", ImVec2(0, ports_h), ImGuiChildFlags_Borders);
            {
                const float half = (ImGui::GetContentRegionAvail().x -
                                    ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                auto port = [&](const char *label, saturn::Peripheral &p) {
                    ImGui::BeginGroup();
                    TextDim("%s", label);
                    ImGui::SetNextItemWidth(half);
                    const char *cur = saturn::peripheral_name(p);
                    if (ImGui::BeginCombo((std::string("##") + label).c_str(), cur)) {
                        for (int i = 0; i <= (int)saturn::Peripheral::ShuttleMouse; ++i) {
                            const auto v = (saturn::Peripheral)i;
                            if (ImGui::Selectable(saturn::peripheral_name(v), p == v)) {
                                p = v;
                                apply_ports();
                                saturn::save_app_config(cfg_path, cfg);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::EndGroup();
                };
                port("PORT 1", cfg.machine.port1);
                ImGui::SameLine();
                port("PORT 2", cfg.machine.port2);
            }
            ImGui::EndChild();
            ImGui::End();
        }

        /* ---- the emulator, filling the window ---- */
        else {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);

            if (show_pause) {
                ImGui::SetNextWindowPos(ImVec2(win_w * 0.5f, win_h * 0.5f),
                                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::Begin("Paused", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::TextUnformatted(loaded_title.c_str());
                TextDim("%d fps", ymir_bridge_get_fps(ymir));
                /* What the app can actually see. "My pad does nothing" has
                 * three causes -- no pad found, the wrong port, or an empty
                 * socket -- and this tells them apart without a rebuild. */
                if (pads.empty()) {
                    TextDim("no gamepad found - keyboard only");
                } else {
                    TextDim("%zu gamepad%s: %s", pads.size(),
                            pads.size() == 1 ? "" : "s",
                            SDL_GetGamepadName(pads[0]) ? SDL_GetGamepadName(pads[0])
                                                        : "unnamed");
                }
                TextDim("port 1: %s   port 2: %s",
                        saturn::peripheral_name(cfg.machine.port1),
                        saturn::peripheral_name(cfg.machine.port2));
                ImGui::Separator();
                const ImVec2 bw(ImGui::GetFontSize() * 12.0f, 0);
                if (ImGui::Button("Resume", bw)) show_pause = false;
                if (ImGui::Button("Reset", bw)) {
                    ymir_bridge_reset(ymir, 1);
                    show_pause = false;
                }
                if (loaded_game_index >= 0 && games[loaded_game_index].multi()) {
                    ImGui::Separator();
                    TextDim("Swap disc");
                    const saturn::Game &g = games[loaded_game_index];
                    for (size_t i = 0; i < g.discs.size(); ++i) {
                        ImGui::PushID((int)i);
                        char lbl[32];
                        snprintf(lbl, sizeof lbl, "Disc %d",
                                 g.discs[i].number ? g.discs[i].number : (int)i + 1);
                        const bool on = (int)i == loaded_disc_index;
                        if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        if (ImGui::Button(lbl, bw)) {
                            insert_disc(loaded_game_index, (int)i);
                            show_pause = false;
                        }
                        if (on) ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Back to the shelf", bw)) {
                    /* Written here too, not only at exit: an app that is
                     * force-quit should not forget the clock. */
                    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
                    running_view = false;
                    show_pause = false;
                }
                ImGui::End();
            }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 14, 16, 23, 255);
        SDL_RenderClear(ren);

        if (running_view && frame) {
            /* 4:3, whatever the window is. The Saturn's modes are not square
             * pixels and a stretched one is simply the wrong picture. */
            const float target = 4.0f / 3.0f;
            float w = (float)win_w, h = w / target;
            if (h > (float)win_h) { h = (float)win_h; w = h * target; }
            const SDL_FRect dst = { ((float)win_w - w) * 0.5f,
                                    ((float)win_h - h) * 0.5f, w, h };
            SDL_RenderTexture(ren, frame, nullptr, &dst);
        }

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
    }

    saturn::save_app_config(cfg_path, cfg);
    /* The clock and the language, so the BIOS does not ask again. */
    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    if (frame) SDL_DestroyTexture(frame);
    if (logo) SDL_DestroyTexture(logo);
    for (SDL_Gamepad *g : pads) if (g) SDL_CloseGamepad(g);
    ymir_bridge_destroy(ymir);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
