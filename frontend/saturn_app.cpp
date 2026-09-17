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

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

extern "C" {
#include "ymir_bridge.h"
}

#include <cstdarg>
#include <cstdio>
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
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::GetIO().IniFilename = nullptr;   /* no imgui.ini beside the binary */
    apply_style();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

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
                    frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
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

        if (!running_view) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
            ImGui::Begin("##shell", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

            const float cw = ImGui::GetContentRegionAvail().x;
            const float fs = ImGui::GetFontSize();

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
    if (frame) SDL_DestroyTexture(frame);
    ymir_bridge_destroy(ymir);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
