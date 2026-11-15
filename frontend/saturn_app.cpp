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
#include "saturn_artwork.h"
#include "saturn_saf.h"
#include "saturn_setup.h"
#include "saturn_lifecycle.h"
#include "touch_pad.h"

#ifndef SATURN_APP_VERSION
#define SATURN_APP_VERSION "2.2.0"
#endif
#ifndef SATURN_APP_BUILD
#define SATURN_APP_BUILD "6"
#endif

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
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <SDL3/SDL.h>
/* Renames main() to SDL_main, which is the symbol SDLActivity looks up on
 * Android. A no-op everywhere else, so there is one main() and one build. */
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

extern "C" {
#include "ymir_bridge.h"
}

#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kWizardDemoStep = 3;

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

/* The same, for the callers that have something to substitute in. */
void TextDimWrappedF(const char *fmt, ...) IM_FMTARGS(1);
void TextDimWrappedF(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.70f, 1.0f));
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
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
    /* The accent is the logo's blue, taken from the mark itself rather than
     * chosen beside it -- an app whose highlight colour disagrees with its own
     * logo looks like two pieces of work. */
    const ImVec4 amber    = ImVec4(0.290f, 0.608f, 0.910f, 1.00f);
    const ImVec4 amberDim = ImVec4(0.290f, 0.608f, 0.910f, 0.35f);

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
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.42f, 0.71f, 0.97f, 1.00f);
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
/*
 * Where this build keeps the files it ships with.
 *
 * Beside the binary on a desktop. On Android there is no "beside the binary" --
 * assets live inside the APK and have no filesystem path at all -- so
 * MainActivity unpacks them into internal storage and this points there.
 */
std::string asset_base(void)
{
#if defined(__ANDROID__)
    if (const char *p = SDL_GetAndroidInternalStoragePath())
        return std::string(p) + "/";
#else
    if (const char *p = SDL_GetBasePath()) return std::string(p);
#endif
    return std::string();
}

SDL_Texture *load_image(SDL_Renderer *ren, const char *path, int *out_w, int *out_h,
                        int max_size = 0, bool trim_transparency = false)
{
    int w = 0, h = 0, comp = 0;
    if (!stbi_info(path, &w, &h, &comp) || w > 8192 || h > 8192) return nullptr;
    stbi_uc *px = stbi_load(path, &w, &h, &comp, 4);
    if (!px) return nullptr;

    const int source_w = w;
    int left = 0, top = 0;
    if (trim_transparency) {
        int right = -1, bottom = -1;
        left = w; top = h;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (px[((size_t)y * source_w + x) * 4 + 3] > 8) {
                    left = std::min(left, x); top = std::min(top, y);
                    right = std::max(right, x); bottom = std::max(bottom, y);
                }
        if (right < left || bottom < top) { stbi_image_free(px); return nullptr; }
        w = right - left + 1;
        h = bottom - top + 1;
    }
    std::vector<unsigned char> thumbnail;
    const unsigned char *pixels = px + ((size_t)top * source_w + left) * 4;
    int pitch = source_w * 4;
    if (max_size > 0 && (w > max_size || h > max_size)) {
        const float ratio = (float)max_size / (float)std::max(w, h);
        const int tw = std::max(1, (int)(w * ratio));
        const int th = std::max(1, (int)(h * ratio));
        thumbnail.resize((size_t)tw * th * 4);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
                std::memcpy(&thumbnail[((size_t)y * tw + x) * 4],
                            &px[((size_t)(top + y * h / th) * source_w + left + x * w / tw) * 4], 4);
        pixels = thumbnail.data();
        w = tw;
        h = th;
        pitch = tw * 4;
    }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, nullptr, pixels, pitch);
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
/*
 * One screen per entry in the rail.
 *
 * These were five faces with the console's five settings pages hidden behind a
 * tab bar inside one of them -- a row of tabs inside a column of buttons, two
 * navigations stacked on top of each other to reach one page. The rail is the
 * navigation now and there is only the one.
 */
enum class Face { Launch, Discs, Saves, Memory,
                  Input, Picture, Sound, Processor, Drive,
                  About, Paths, Setup };

/* While the Pad page is on its Review tab, the rail collapses so the
 * 16:9 preview takes the whole window. File-scope so the rail
 * rendering and the SDL event loop can both read it. 2026-09-18. */
static int g_pad_fullscreen = 0;
static int g_pad_tab = 0;

char title_initial(const std::string &title)
{
    for (char c : title) {
        if (c == ' ') continue;
        const char u = (char)SDL_toupper((unsigned char)c);
        return (u >= 'A' && u <= 'Z') ? u : '#';
    }
    return '#';
}

/*
 * The A-Z strip itself. `letter` is 0 for "everything", otherwise the initial
 * to show. Only the letters something is actually filed under are offered --
 * a row of twenty-six buttons of which four do anything is a row of twenty-two
 * dead ends.
 */
bool letter_strip(const std::string &have, char &letter, float width)
{
    bool changed = false;
    /*
     * Small, because there can be twenty-seven of them.
     *
     * At the touch padding the rest of the interface uses, the strip was two
     * rows of buttons the size of the shelf's own cards and pushed the discs
     * off the bottom of the screen. An initial is one character; it needs a
     * key, not a button.
     */
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetFontSize() * 0.22f,
                               ImGui::GetFontSize() * 0.16f));
    /* Letters get a square; "All" gets whatever the word needs. A fixed width
     * for both clipped it to "Al", which is not a word. */
    auto key = [&](const char *label, char c, float w) {
        const bool on = (letter == c);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, ImVec2(w, 0))) {
            letter = on ? 0 : c;      /* pressing the live one clears it */
            changed = true;
        }
        if (on) ImGui::PopStyleColor();
    };
    const float square = ImGui::GetFontSize() * 1.25f;
    /* Wrap against what is actually available, not against a width passed in
     * from somewhere further out: the strip lives inside a padded child and
     * the last letter was landing past the edge. */
    (void)width;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    key("All", 0, 0.0f);
    for (char c : have) {
        const char lbl[2] = { c, 0 };
        const float step = square + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::GetCursorPosX() + step < right) ImGui::SameLine();
        key(lbl, c, square);
    }
    ImGui::PopStyleVar();
    return changed;
}

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
 * along the top -- plus L and R shoulders. A modern pad has four faces, two
 * bumpers and two triggers, so something has to give.
 *
 * The bumpers carry the Saturn's own shoulders, L and R, which is where a hand
 * expects them: in Daytona they are the gear change, and a gear change under a
 * bumper is right where every racing game since has put it. The bumpers used
 * to carry C and Z, which put two face buttons on the shoulders and left the
 * Saturn's actual shoulders on the triggers -- the wrong way round, and it
 * felt it.
 *
 * The triggers are the pedals: right for accelerate, left for brake, which on
 * the Saturn pad are A and B. They are a second way of pressing those two, not
 * a replacement -- A and B stay on the face buttons as well, so a game that
 * wants them as buttons still has them under a thumb. That is why presses are
 * tracked per source below: two controls driving one Saturn button must not
 * cancel each other when one is released.
 *
 * C and Z go to the stick clicks. Nothing else is left, and a six-button game
 * that needs them is being played on the wrong pad anyway.
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
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  YMIR_BUTTON_L     },
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, YMIR_BUTTON_R     },
    { SDL_GAMEPAD_BUTTON_LEFT_STICK,     YMIR_BUTTON_Z     },
    { SDL_GAMEPAD_BUTTON_RIGHT_STICK,    YMIR_BUTTON_C     },
};

/* Left trigger brakes, right trigger accelerates. */
const YmirButton kTriggerButton[2] = { YMIR_BUTTON_B, YMIR_BUTTON_A };

/*
 * Where the Saturn's picture lands in the window, and how it is filtered.
 *
 * Worked out in one place because two things need it: the blit, obviously,
 * and the light gun, which has to turn a mouse position in the window back
 * into a pixel on the Saturn's screen. Two copies of this arithmetic would be
 * two copies to keep in step, and a gun that aims a few pixels off the thing
 * you are pointing at is worse than no gun.
 */
struct PictureRect { float x, y, w, h; };

PictureRect picture_rect(int win_w, int win_h, int fw, int fh,
                         const saturn::Settings &s)
{
    float w = (float)win_w, h = (float)win_h;
    if (s.aspect == 0) {
        const float target = 4.0f / 3.0f;
        h = w / target;
        if (h > (float)win_h) { h = (float)win_h; w = h * target; }
    }
    if (s.integer_scale && fw > 0 && fh > 0) {
        int k = (int)std::min(w / (float)fw, h / (float)fh);
        if (k < 1) k = 1;
        w = (float)(fw * k);
        h = (float)(fh * k);
    }
    return { ((float)win_w - w) * 0.5f, ((float)win_h - h) * 0.5f, w, h };
}

bool picture_linear(const PictureRect &r, int fw, int fh,
                    const saturn::Settings &s)
{
    switch (s.scaling) {
    case 1:  return false;
    case 2:  return true;
    default:
        /* Auto: hard pixels only when the scale divides evenly. */
        return fw <= 0 || fh <= 0 ||
               (int)r.w % fw != 0 || (int)r.h % fh != 0;
    }
}

/* The triggers are axes, not buttons, so they are read rather than bound.
 * Half travel counts as pressed: an analogue trigger standing in for a digital
 * button should fire where a foot expects it to, not at the very bottom. */
const Sint16 kTriggerOn = 16384;

/*
 * Which sources are holding each Saturn button down, per port.
 *
 * The triggers and the face buttons overlap now, and without this a driver
 * resting on the accelerator who taps A and lets go would have the car stop:
 * the release of one source would clear a button the other was still holding.
 * One bit per source, and the Saturn sees the button as pressed while any bit
 * is set.
 */
enum { kSrcButton = 1 << 0, kSrcTrigger = 1 << 1, kSrcStick = 1 << 2,
       kSrcTouch = 1 << 3 };
uint8_t g_held[2][YMIR_BUTTON_COUNT] = {};

void pad_press(YmirInstance *ymir, int port, YmirButton button,
               unsigned source, bool down)
{
    if (port < 1 || port > 2) return;
    uint8_t &mask = g_held[port - 1][button];
    const uint8_t before = mask;
    if (down) mask |= (uint8_t)source;
    else      mask &= (uint8_t)~source;
    if ((before != 0) != (mask != 0))
        ymir_bridge_set_pad_button(ymir, port, button, mask != 0);
}

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
    /*
     * Landscape, and only landscape.
     *
     * The manifest already asks for sensorLandscape, and on its own that is
     * not enough: SDL tells the activity which orientations it wants when the
     * window is created, and for a RESIZABLE window with no hint set it asks
     * for UNSPECIFIED -- which overrides the manifest and lets the device
     * rotate into portrait. A Saturn picture is 4:3 and every screen in this
     * application is laid out across; turned on its side it is unusable.
     *
     * Both landscapes, not one: a handheld held either way up is still a
     * handheld, and people do hold them either way up.
     */
#if defined(SATURN_IOS)
    // iPad scenes resize and rotate; the iPhone plist remains landscape-only.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
#else
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#if defined(SATURN_IOS)
    window_flags |= SDL_WINDOW_BORDERLESS;
#endif
    if (!SDL_CreateWindowAndRenderer("Retro-Saturn", 1280, 800,
                                     window_flags, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    /*
     * No ImGui navigation at all, by keyboard or by pad.
     *
     * Turning off the gamepad half stopped the pad being consumed, but the
     * interface still walked and activated itself -- the wizard advanced a
     * step and opened a file dialog with nobody touching it. Whatever is
     * feeding it, this app does not need the feature: it is driven by a mouse,
     * and the keys it does want are handled by hand, in the emulator view,
     * where they are sent to the Saturn.
     *
     * The cost of nav being on here is an interface that operates itself. The
     * cost of it being off is that Tab does not move a focus ring. That is not
     * a difficult trade.
     */
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

    bool has_touch = false;
    {
        float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(win));
        if (scale <= 0.0f) scale = 1.0f;
        const float pt = std::clamp(18.0f * scale, 14.0f, 40.0f);
        const std::string font = asset_base() + "assets/ui-font.ttf";
        if (!ImGui::GetIO().Fonts->AddFontFromFileTTF(font.c_str(), pt)) {
            ImFontConfig font_config;
            font_config.SizePixels = pt;
            ImGui::GetIO().Fonts->AddFontDefault(&font_config);
        }
        apply_style();
#if defined(__ANDROID__) || defined(SATURN_IOS)
        has_touch = true;
#else
        int count = 0;
        SDL_free(SDL_GetTouchDevices(&count));
        has_touch = count > 0;
#endif
        if (has_touch) {
            ImGuiStyle &style = ImGui::GetStyle();
            style.FramePadding = ImVec2(14.0f, 12.0f);
            style.ItemSpacing = ImVec2(12.0f, 10.0f);
            style.ScrollbarSize = 26.0f;
            style.GrabMinSize = 22.0f;
        }
        ImGui::GetStyle().ScaleAllSizes(std::clamp(scale, 1.0f, 2.5f));
    }

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

    saturn::restore_folder_access();
    saturn::AppConfig cfg;
    if (cfg.disc_root.empty()) {
        /* A sensible first guess, created on demand rather than at startup:
         * an empty folder the user never asked for is litter. */
        if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
            cfg.disc_root = std::string(home) + "Saturn";
    }
    saturn::load_app_config(cfg_path, cfg);
    for (std::string *path : {&cfg.parent_root, &cfg.disc_root, &cfg.bios_path, &cfg.artwork_dir, &cfg.saves_dir})
        *path = saturn::resolve_stored_path(*path);

    if (cfg.disc_tree.empty()) {
        if (cfg.parent_root.empty()) cfg.parent_root = saturn::parent_for_discs(cfg.disc_root);
        if (cfg.artwork_dir.empty()) cfg.artwork_dir = saturn::folder_for(cfg.parent_root, "artwork");
        SDL_PathInfo info;
        if (SDL_GetPathInfo(cfg.parent_root.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
            SDL_CreateDirectory(cfg.artwork_dir.c_str());
    } else {
        saturn::saf_ensure_layout(cfg.disc_tree);
    }
    saturn::ArtworkLibrary artwork;
    unsigned artwork_revision = 1;
    artwork.scan(cfg.artwork_dir, cfg.disc_tree);

    bool demo_session = false;
    bool demo_tested = false;
    std::string demo_error;
    const std::string demo_saves_dir = cfg_dir + "wizard-demo";
    const std::string demo_bram_path = demo_saves_dir + "/backup-ram.bin";
    const std::string demo_restore_path = demo_saves_dir + "/original-backup-ram.bin";
    auto demo_game = [&] {
        saturn::Game g;
        saturn::Disc d;
        d.path = saturn::demo_disc_path();
        d.file = "PPPong.cue";
        g.title = saturn::demo_title();
        g.discs.push_back(d);
        return g;
    };

    /* ---- what is in the drive ---- */
    std::string loaded_title;     /* empty = no disc */
    std::string loaded_path;
    std::string loaded_tree;
    int  loaded_disc_index = 0;
    int  loaded_game_index = -1;
    std::string message;          /* one line, shown under the drive */

    std::vector<saturn::Game> games;
    /*
     * The shelf comes from a path, or from a granted folder, never both.
     *
     * A folder Android granted has no path, so it is scanned over the document
     * tree instead; everything downstream is the same, because a disc from
     * there carries its tree with it and is staged when it is actually
     * started.
     */
    auto rescan = [&] {
        if (!cfg.disc_tree.empty())
            games = saturn::scan_discs_saf(cfg.disc_tree, "cd");
        else
            games = saturn::scan_discs(cfg.disc_root);
        if (demo_session) games.insert(games.begin(), demo_game());
        artwork.scan(cfg.artwork_dir, cfg.disc_tree);
        ++artwork_revision;
        // Adding artwork or discs must not change which game is in the drive.
        loaded_game_index = -1;
        for (size_t i = 0; i < games.size(); ++i)
            for (size_t d = 0; d < games[i].discs.size(); ++d) {
                const auto &disc = games[i].discs[d];
                if ((!loaded_path.empty() && disc.path == loaded_path) ||
                    (!loaded_tree.empty() && disc.tree == loaded_tree && disc.file == base_name(loaded_path))) {
                    loaded_game_index = (int)i;
                    loaded_disc_index = (int)d;
                }
            }
        if (loaded_game_index < 0) {
            loaded_title.clear();
            loaded_path.clear();
            loaded_tree.clear();
        }
    };
    rescan();

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
        ymir_bridge_set_audio_muted(ymir, s.audio_muted);
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

    /*
     * A BIOS nobody had to go and find.
     *
     * If one is sitting in the discs folder and none is configured, use it.
     * On Android that is the whole route -- there is no usable file picker
     * there and the folder is where everything goes anyway -- and on the
     * desktop it saves a step for the common case of keeping the BIOS beside
     * the games. Never overrides a choice already made.
     */
    /*
     * One folder, laid out.
     *
     * Android grants a folder at a time, so the app asks for exactly one and
     * makes `bios`, `cd` and `saves` inside it -- somewhere obvious for each
     * thing rather than a folder that works but whose shape nobody can guess.
     * Existing folders are left alone; this creates, it never tidies.
     *
     * When the granted folder turns out to have a real path -- which it does
     * whenever it is somewhere the app can already reach -- everything uses
     * that directly and nothing is ever copied. Otherwise discs are staged one
     * at a time as they are started, and only then.
     */
    auto adopt_tree = [&](const std::string &tree) {
        if (tree.empty()) return;
        cfg.disc_tree = tree;
        saturn::saf_ensure_layout(tree);

        std::string real;
        for (const saturn::SafTree &t : saturn::saf_trees())
            if (t.uri == tree) real = t.path;

        if (!real.empty()) {
            saturn::configure_parent_folder(cfg, real);
        } else {
            cfg.parent_root.clear();
            cfg.artwork_dir.clear();
            cfg.disc_root.clear();
            cfg.saves_dir.clear();
        }
        saturn::save_app_config(cfg_path, cfg);
    };

    auto adopt_bios_from_discs = [&] {
        if (bios_loaded) return;

        /*
         * Out of the granted folder's bios/ when there is one.
         *
         * 512 KB, so staging it costs nothing worth measuring and the result
         * is a real path the core can open. This is the whole route on
         * Android: there is no usable file picker there, and a BIOS in the
         * folder you granted is where it should be anyway.
         */
        if (!cfg.disc_tree.empty()) {
            for (const saturn::SafEntry &e : saturn::saf_list(cfg.disc_tree, "bios")) {
                if (e.bytes != 512 * 1024) continue;
                const std::string cache = cfg_dir + "bios";
                SDL_CreateDirectory(cache.c_str());
                const std::string got =
                    saturn::saf_stage(cfg.disc_tree, "bios", e.name, cache);
                if (got.empty()) continue;
                cfg.bios_path = got;
                if (load_bios()) saturn::save_app_config(cfg_path, cfg);
                return;
            }
            return;
        }

        if (cfg.disc_root.empty()) return;
        /* A path: the bios folder beside the discs first, whatever it is
         * called, then among the discs themselves. */
        const std::string parent = cfg.parent_root;
        std::string found = saturn::find_bios_in(saturn::folder_for(parent, "bios"));
        if (found.empty()) found = saturn::find_bios_in(cfg.disc_root);
        if (found.empty()) return;
        cfg.bios_path = found;
        if (load_bios()) saturn::save_app_config(cfg_path, cfg);
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
    if (ymir_bridge_load_smpc_state(ymir, smpc_path.c_str()) != YMIR_OK) {
        /*
         * First run: set the machine up rather than making the user do it.
         *
         * A fresh Saturn has never had its clock or language set, so the BIOS
         * asks -- every boot, about a date this device already knows. There is
         * nothing to be gained from making somebody type it in, so the clock
         * is taken from the host and the console is marked as configured.
         * The BIOS screen is still there in its own menu for anyone who wants
         * it.
         */
        ymir_bridge_init_smpc_from_host(ymir, 0);
        ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    }

    /*
     * ---- where saves live ----
     *
     * Not in the app's own storage, and this is the whole point of the
     * setting. On Android the app's storage belongs to the install: uninstall
     * it, or sideload a new build over the top in a way Android treats as a
     * fresh install, and every save file goes with it. It is also not
     * somewhere a person can reach to take a backup.
     *
     * The discs folder is different. The user chose it and granted it, it sits
     * outside the sandbox, it survives an update or a reinstall, and they can
     * copy it off the device. So saves go in a folder beside the discs, and
     * the setting exists for anyone who wants them somewhere else again.
     *
     * Two kinds of thing end up here and they are not the same: backup-ram.bin
     * is the Saturn's own battery-backed memory, which is where a game writes
     * its progress, and states/ holds our snapshots of the whole machine. The
     * first is the one that must never be lost.
     */
    std::string saves_dir, states_dir, bram_path, active_saves_tree;
    bool saves_sync_failed = false;
    auto sync_saves = [&] {
        if (demo_session || active_saves_tree.empty()) return true;
        const bool okay = saturn::saf_sync_saves(active_saves_tree, saves_dir);
        saves_sync_failed = !okay;
        return okay;
    };
    auto resolve_saves = [&] {
        active_saves_tree = cfg.disc_tree;
        saves_dir = cfg.saves_dir;
        if (!active_saves_tree.empty()) {
            uint64_t hash = 14695981039346656037ULL;
            for (unsigned char c : active_saves_tree) { hash ^= c; hash *= 1099511628211ULL; }
            char suffix[32];
            snprintf(suffix, sizeof suffix, "saves-%016llx", (unsigned long long)hash);
            saves_dir = cfg_dir + suffix;
            SDL_CreateDirectory(saves_dir.c_str());
            saves_sync_failed = !saturn::saf_restore_saves(active_saves_tree, saves_dir);
            // Carry forward battery RAM from releases that only kept a private fallback.
            const std::string old_ram = cfg_dir + "Saves/backup-ram.bin";
            const std::string migrated = cfg_dir + "saves-cache-migrated";
            if (!path_is_file(saves_dir + "/backup-ram.bin") && !path_is_file(migrated) && path_is_file(old_ram)) {
                if (SDL_CopyFile(old_ram.c_str(), (saves_dir + "/backup-ram.bin").c_str())) {
                    if (SDL_IOStream *marker = SDL_IOFromFile(migrated.c_str(), "wb")) SDL_CloseIO(marker);
                }
            }
        }
        if (saves_dir.empty()) {
            saves_dir = cfg.disc_root.empty() ? (cfg_dir + "Saves")
                                              : (cfg.disc_root + "/Saves");
        }
        states_dir = saves_dir + "/states";
        bram_path  = saves_dir + "/backup-ram.bin";
        SDL_CreateDirectory(saves_dir.c_str());
        SDL_CreateDirectory(states_dir.c_str());
    };
    resolve_saves();

    /* Anything written by an earlier build, brought along. The old location
     * was inside the app, which is exactly what this change is getting away
     * from -- leaving states behind there would be the loss this is meant to
     * prevent. */
    {
        const std::string old_states = cfg_dir + "states";
        SDL_PathInfo info;
        if (old_states != states_dir &&
            SDL_GetPathInfo(old_states.c_str(), &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY) {
            int count = 0;
            char **found = SDL_GlobDirectory(old_states.c_str(), "*", 0, &count);
            for (int i = 0; found && i < count; ++i) {
                const std::string from = old_states + "/" + found[i];
                const std::string to   = states_dir + "/" + found[i];
                if (!path_is_file(to)) SDL_RenamePath(from.c_str(), to.c_str());
            }
            if (found) SDL_free(found);
            if (count > 0)
                SDL_Log("moved %d save state(s) out of the app's own storage", count);
        }
    }

    apply_options();
    apply_ports();
    load_bios();
    adopt_bios_from_discs();

    /*
     * The Saturn's memory card, such as it is: 32 KiB of battery-backed SRAM
     * inside the machine, and where every game that saves anything puts it.
     * Nothing was loading or saving it at all, so progress was being lost on
     * exit -- the emulator was a console with a flat battery.
     *
     * Loaded after the BIOS because loading the BIOS hard-resets. Not
     * copy-on-write: writes go to the file as the game makes them, so a
     * crash costs nothing.
     */
    auto load_bram = [&] {
        ymir_bridge_load_internal_backup_memory(ymir,
            (demo_session ? demo_bram_path : bram_path).c_str(), 0);
    };
    auto save_bram = [&] {
        const int32_t result = ymir_bridge_save_internal_backup_memory(ymir,
            (demo_session ? demo_bram_path : bram_path).c_str());
        if (result == YMIR_OK) sync_saves();
    };
    load_bram();

    auto insert_disc = [&](int gameIndex, int discIndex) {
        if (gameIndex < 0 || gameIndex >= (int)games.size()) return;
        const saturn::Game &g = games[gameIndex];
        const saturn::Disc *d = g.disc((size_t)discIndex);
        if (!d) return;
        if (d->path == saturn::demo_disc_path() && !demo_session) {
            message = "Run the bundled demo from the setup wizard.";
            return;
        }
        if (!bios_loaded) {
            message = "No BIOS loaded. The Saturn cannot start without one.";
            return;
        }
        /*
         * A disc from a granted folder has no path until it has one.
         *
         * Android hands out a permission slip, not a filename, and ymir opens
         * a disc by path -- so the one being started is copied into the app's
         * own storage first. Once, and cached by name and size: the second
         * time the same game is chosen it starts immediately.
         *
         * Only the disc being started, never the shelf. Copying a library of
         * these would duplicate tens of gigabytes to no purpose.
         */
        std::string disc_path = d->path;
        if (disc_path.empty() && !d->tree.empty()) {
            const std::string cache = cfg_dir + "discs";
            SDL_CreateDirectory(cache.c_str());
            message = "Copying " + d->file + " from the folder you granted...";
            disc_path = saturn::saf_stage_disc(d->tree, "cd", d->file, cache);
            if (disc_path.empty()) {
                message = "Could not copy " + d->file;
                return;
            }
            message.clear();
        }

        const int32_t rc = ymir_bridge_load_disc(ymir, disc_path.c_str());
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
        loaded_path = disc_path;
        loaded_tree = d->tree;
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
    /* The hardware, photographed. The machine goes on the drive panel and the
     * peripherals go on the ports, so what is plugged into the Saturn is
     * something you recognise at a glance rather than something you read. */
    SDL_Texture *art_console = nullptr; int art_console_w = 0, art_console_h = 0;

    /*
     * A photograph per socket, indexed by what is plugged into it.
     *
     * A table rather than a variable each, because the question "which
     * picture is this peripheral's" then has exactly one answer and adding a
     * render is one filename in one place. It was a switch, and the switch
     * answered "the Control Pad" for everything that was not a gun -- so the
     * picture never changed as you stepped through the ports, and a Shuttle
     * Mouse was illustrated with a photograph of a pad.
     *
     * Every peripheral the Saturn has now has its own render; the only null
     * is the empty socket, which is drawn rather than photographed. If one
     * ever fails to load, that port falls back to a plate with the device's
     * name on it -- honest, and it still tells you the port is taken.
     */
    struct Photo { SDL_Texture *tex = nullptr; int w = 0, h = 0; };
    static const char *const kPeriphArt[] = {
        nullptr,             /* None -- an empty socket is drawn, not photographed */
        "control-pad.png",   /* ControlPad */
        "analog-pad.png",    /* AnalogPad */
        "arcade-racer.png",  /* ArcadeRacer */
        "mission-stick.png", /* MissionStick */
        "virtua-gun.png",    /* VirtuaGun */
        "shuttle-mouse.png", /* ShuttleMouse */
    };
    static_assert(sizeof(kPeriphArt) / sizeof(kPeriphArt[0]) ==
                      (size_t)saturn::Peripheral::ShuttleMouse + 1,
                  "a peripheral was added without saying what it looks like");
    Photo art_periph[sizeof(kPeriphArt) / sizeof(kPeriphArt[0])];

    auto art_for = [&](saturn::Peripheral p, int *w, int *h) -> SDL_Texture * {
        const size_t i = (size_t)p;
        if (i >= sizeof(art_periph) / sizeof(art_periph[0])) {
            *w = 0; *h = 0; return nullptr;
        }
        *w = art_periph[i].w; *h = art_periph[i].h;
        return art_periph[i].tex;
    };
    {
        const std::string base = asset_base();
        logo = load_image(ren, (base + "assets/wordmark.png").c_str(),
                        &logo_w, &logo_h, 1024, true);
        art_console = load_image(ren, (base + "assets/console.png").c_str(),
                               &art_console_w, &art_console_h, 640, true);
        for (size_t i = 0; i < sizeof(art_periph) / sizeof(art_periph[0]); ++i) {
            if (!kPeriphArt[i]) continue;
            art_periph[i].tex = load_image(ren, (base + "assets/" + kPeriphArt[i]).c_str(),
                                         &art_periph[i].w, &art_periph[i].h, 320, true);
        }
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

    /* The machine, first: what is in the drive and the button that starts it
     * is the thing somebody opened the application to do. */
    Face face = Face::Launch;
    char disc_letter = 0;          /* A-Z strip on the shelf; 0 = everything */
    bool shelf_grid = true;
    struct Art { SDL_Texture *tex = nullptr; int w = 0, h = 0; };
    std::map<std::string, Art> art;
    unsigned textures_revision = 0;
    auto game_art = [&](const saturn::Game &game) -> const Art & {
        if (textures_revision != artwork_revision) {
            for (auto &kv : art) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
            art.clear();
            textures_revision = artwork_revision;
        }
        auto [it, added] = art.try_emplace(game.title);
        if (added) {
            const std::string path = artwork.path_for(game, cfg_dir + "artwork-cache");
            if (!path.empty())
                it->second.tex = load_image(ren, path.c_str(), &it->second.w, &it->second.h, 512);
        }
        return it->second;
    };

    bool library_drag_item = false;
    bool library_dragged = false;
    auto library_tap = [&] {
        const ImVec2 drag = ImGui::GetMouseDragDelta(0, ImGui::GetFontSize() * 0.5f);
        return !library_dragged && drag.x == 0.0f && drag.y == 0.0f;
    };
    auto scroll_library = [&] {
        const float threshold = ImGui::GetFontSize() * 0.5f;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            (library_drag_item || !ImGui::IsAnyItemActive()) && ImGui::IsMouseDragging(0, threshold)) {
            library_dragged = true;
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }
    };
    auto cover_card = [&](const saturn::Game &game, const char *sub, float card_w) -> bool {
        const float cover_h = card_w * 1.42f;
        const float title_h = ImGui::GetTextLineHeight() * 2.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const float card_h = cover_h + spacing + title_h + spacing + ImGui::GetTextLineHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(card_w, card_h));
        library_drag_item |= ImGui::IsItemActive();
        const bool hit = clicked && library_tap();
        if (!ImGui::IsItemVisible()) return hit;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const Art &a = game_art(game);
        if (a.tex) {
            float w = card_w, h = w * (float)a.h / (float)a.w;
            if (h > cover_h) { h = cover_h; w = h * (float)a.w / (float)a.h; }
            dl->AddImage((ImTextureID)(intptr_t)a.tex,
                         ImVec2(at.x + (card_w - w) * 0.5f, at.y + (cover_h - h) * 0.5f),
                         ImVec2(at.x + (card_w + w) * 0.5f, at.y + (cover_h + h) * 0.5f));
        } else {
            dl->AddRectFilled(at, ImVec2(at.x + card_w, at.y + cover_h), IM_COL32(28, 32, 44, 255), 4.0f);
            dl->AddRect(at, ImVec2(at.x + card_w, at.y + cover_h), IM_COL32(60, 70, 92, 255), 4.0f);
        }
        // A title is a label, never a nested scrolling window.
        std::string label = game.title;
        size_t end = game.title.size();
        while (end > 0 && ImGui::CalcTextSize(label.c_str(), nullptr, false, card_w).y > title_h) {
            --end;
            while (end > 0 && ((unsigned char)game.title[end] & 0xC0) == 0x80) --end;
            label = game.title.substr(0, end) + "...";
        }
        const ImVec2 text_at(at.x, at.y + cover_h + spacing);
        dl->PushClipRect(text_at, ImVec2(text_at.x + card_w, text_at.y + title_h), true);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_at, ImGui::GetColorU32(ImGuiCol_Text), label.c_str(), nullptr, card_w);
        dl->PopClipRect();
        if (sub && *sub)
            dl->AddText(ImVec2(at.x, text_at.y + title_h + spacing), IM_COL32(158, 163, 178, 255), sub);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::GetIO().MouseSource != ImGuiMouseSource_TouchScreen)
            ImGui::SetTooltip("%s", game.title.c_str());
        return hit;
    };

    /* The wizard runs until it is finished once, and can be asked for again
     * from Console. A disc on the command line skips it: somebody who
     * double-clicked a game has answered the only question it asks. */
    bool wizard = !cfg.wizard_done && loaded_game_index < 0;
    int  wstep = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--wizard") wizard = true;
        if (arg.rfind("--wizard-step=", 0) == 0) {
            wizard = true;
            wstep = std::clamp(std::atoi(arg.c_str() + 14), 0, kWizardDemoStep + 1);
        }
    }
    bool running_view = loaded_game_index >= 0;   /* given a disc: play it */
    bool show_pause = false;

    /*
     * The on-screen pad: retro_touch_pad's overlay over port 1.
     *
     * Its ids are the profile's, not the Saturn's, so one table turns them
     * into buttons. The three pads share ids where they mean the same thing
     * (a, b, x, y, start) and differ where they do not: the joystick's fire
     * 1 is A and fire 2 is B, the two buttons most games put jump and shoot
     * on; the 360 pad's bumpers are L and R and its triggers take the two
     * face buttons the 360 has not got -- LT is Z, RT is C. Back opens the
     * pause menu, as on a real pad. Directions come already merged.
     */
    touchpad::Overlay pad;
    bool pad_editing = false;            /* arranging, in a game */
    ImVec2 pad_preview_pos(0, 0), pad_preview_size(0, 0);   /* the settings page's box */
    auto touch_button = [](const std::string &id) -> int {
        static const struct { const char *id; YmirButton b; } map[] = {
            { "fire1", YMIR_BUTTON_A }, { "fire2", YMIR_BUTTON_B },
            { "a", YMIR_BUTTON_A }, { "b", YMIR_BUTTON_B }, { "c", YMIR_BUTTON_C },
            { "x", YMIR_BUTTON_X }, { "y", YMIR_BUTTON_Y }, { "z", YMIR_BUTTON_Z },
            { "l", YMIR_BUTTON_L }, { "r", YMIR_BUTTON_R },
            { "lb", YMIR_BUTTON_L }, { "rb", YMIR_BUTTON_R },
            { "lt", YMIR_BUTTON_Z }, { "rt", YMIR_BUTTON_C },
            { "start", YMIR_BUTTON_START },
        };
        for (const auto &m : map) if (id == m.id) return (int)m.b;
        return -1;
    };
    touchpad::Sink pad_sink;
    pad_sink.directions = [&](bool u, bool d, bool l, bool r) {
        pad_press(ymir, 1, YMIR_BUTTON_UP, kSrcTouch, u);
        pad_press(ymir, 1, YMIR_BUTTON_DOWN, kSrcTouch, d);
        pad_press(ymir, 1, YMIR_BUTTON_LEFT, kSrcTouch, l);
        pad_press(ymir, 1, YMIR_BUTTON_RIGHT, kSrcTouch, r);
    };
    pad_sink.action = [&](const std::string &id, bool down) {
        /* Back opens the menu, as it does on a real pad: a handheld has no
         * Escape key and a thumb on glass should not need one. */
        if (id == "back") { if (down) show_pause = true; return; }
        const int b = touch_button(id);
        if (b >= 0) pad_press(ymir, 1, (YmirButton)b, kSrcTouch, down);
    };
    /* The settings page's preview presses nothing. */
    touchpad::Sink pad_silent;
    pad_silent.directions = [](bool, bool, bool, bool) {};
    pad_silent.action = [](const std::string &, bool) {};
    auto load_pad = [&] {
        const touchpad::Profile *pp = touchpad::profile_by_id(cfg.machine.touch_pad);
        if (!pp) pp = &touchpad::profile_xbox360();
        pad.set(pp, touchpad::Layout::load(cfg_dir, *pp));
    };
    load_pad();
    /* Shown when asked, or by default where there is a touchscreen -- and
     * never over a gun or a mouse game, which have no use for it and whose
     * pointer would fight it for every finger. */
    auto pad_on = [&] {
        if (cfg.machine.touch_pad_show == 2) return false;
        if (cfg.machine.touch_pad_show == 0 && !has_touch) return false;
        return cfg.machine.port1 != saturn::Peripheral::VirtuaGun &&
               cfg.machine.port1 != saturn::Peripheral::ShuttleMouse;
    };

    /*
     * The machine only runs while you are looking at it.
     *
     * It used to keep running behind the shelf, which you could hear: a game
     * carried on playing its music while you browsed for another one. It also
     * burned a core for nothing. The pause stops the audio device as well as
     * the emulation, and the mailbox is still served while paused, so swapping
     * a disc or saving a state from the menu still works.
     */
    saturn::AppLifecycle lifecycle(SDL_GetWindowID(win), [ymir](bool paused) {
        ymir_bridge_set_presentation_paused(ymir, paused ? 1 : 0);
    });
    lifecycle.set_playing(running_view);

    /*
     * The gun, and the mouse.
     *
     * A Virtua Gun is a pointing device and a host has two of them: a mouse
     * and a finger. Both are handled the same way -- a position in window
     * pixels and a trigger -- because from the Saturn's side there is no
     * difference. The position is turned into a Saturn pixel at the point of
     * use, through the same rectangle the picture is drawn into, so aiming
     * stays honest whatever the window is doing.
     *
     * Right button reloads. On a real Virtua Gun reloading means pointing off
     * the screen and pulling the trigger; on a mouse there is no off-screen
     * worth speaking of and on a touch screen there is none at all, so it
     * gets a button of its own.
     */
    struct Pointer {
        float x = 0, y = 0;
        bool  trigger = false, reload = false, start = false;
        bool  seen = false;          /* has ever been pointed at the window */
    } pointer;
    int  mouse_dx = 0, mouse_dy = 0; /* for the Shuttle Mouse, which is relative */

    /* Which port, if either, is holding a pointing device. */
    auto port_with = [&](saturn::Peripheral want) -> int {
        if (cfg.machine.port1 == want) return 1;
        if (cfg.machine.port2 == want) return 2;
        return 0;
    };

    std::string state_message;
    Uint64 state_message_at = 0;

    /*
     * A save is a file, not a slot.
     *
     * Four numbered slots meant deciding, every time, which of your own saves
     * to destroy -- and the decision came up at the worst moment, with the
     * game paused mid-fight. Saving now writes a new file stamped with the
     * time, so nothing is ever overwritten and there is no limit but the
     * card. The Saves page is where they are read back and deleted, which is
     * a thing to do at leisure rather than at a boss.
     *
     * The stamp is YYYYMMDD-HHMMSS so the newest is also the last in
     * alphabetical order: "which is most recent" needs no date parsing, on a
     * filesystem whose timestamps we cannot always trust.
     */
    auto state_stem = [&]() {
        std::string stem = base_name(loaded_path);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.erase(dot);
        for (char &c : stem)
            if (c == '/' || c == '\\' || c == ':') c = '_';
        return stem;
    };

    auto state_new_path = [&]() {
        SDL_Time now = 0;
        SDL_DateTime dt{};
        char when[32] = "00000000-000000";
        if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true))
            snprintf(when, sizeof when, "%04d%02d%02d-%02d%02d%02d",
                     dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        return states_dir + "/" + state_stem() + "." + when + ".state";
    };

    /* The newest save for what is in the drive, or empty if there is none.
     * Old .s1-.s4 files count: somebody who saved before this changed should
     * not find their saves gone. */
    auto state_latest = [&]() {
        const std::string stem = state_stem();
        std::string best;
        int n = 0;
        char **found = SDL_GlobDirectory(states_dir.c_str(), "*", 0, &n);
        for (int i = 0; found && i < n; ++i) {
            const std::string name = found[i];
            if (name.size() <= stem.size() + 1 || name.compare(0, stem.size(), stem) != 0
                || name[stem.size()] != '.') continue;
            if (name > best) best = name;
        }
        if (found) SDL_free(found);
        return best.empty() ? std::string() : states_dir + "/" + best;
    };

    auto say = [&](const char *what, int32_t rc) {
        char buf[160];
        if (rc == YMIR_OK) snprintf(buf, sizeof buf, "%s", what);
        else               snprintf(buf, sizeof buf, "%s failed (%d)", what, rc);
        state_message = buf;
        state_message_at = SDL_GetTicks();
    };

    auto do_save_state = [&]() {
        if (loaded_game_index < 0) return;
        say("Saved", ymir_bridge_save_state(ymir, state_new_path().c_str()));
        save_bram();
    };
    auto do_load_state = [&]() {
        if (loaded_game_index < 0) return;
        const std::string p = state_latest();
        if (p.empty()) { say("Nothing saved for this game", -1); return; }
        say("Loaded the last save", ymir_bridge_load_state(ymir, p.c_str()));
    };
    char search[96] = {0};
    bool quit = false;

    auto return_to_wizard = [&] {
        if (!demo_session) return;
        lifecycle.set_playing(false);
        pad.release_all(pad_sink);
        save_bram();
        ymir_bridge_eject_disc(ymir);
        demo_session = false;
        demo_tested = true;
        resolve_saves();
        if (ymir_bridge_load_internal_backup_memory(ymir, demo_restore_path.c_str(), 1) == YMIR_OK) {
            // Restore the normal write-through file after leaving the private demo RAM.
            if (ymir_bridge_save_internal_backup_memory(ymir, bram_path.c_str()) == YMIR_OK)
                load_bram();
        } else {
            load_bram();
        }
        rescan();
        loaded_game_index = -1;
        loaded_disc_index = 0;
        loaded_title.clear();
        loaded_path.clear();
        loaded_tree.clear();
        show_pause = false;
        running_view = false;
        pointer.seen = false;
        wizard = true;
        wstep = kWizardDemoStep;
    };

    while (!quit) {
        /* Read once, at the top: the events below need it too -- a touch
         * arrives in normalised coordinates and has nothing to scale by
         * otherwise. */
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(win, &win_w, &win_h);

        const bool pad_active = running_view && !show_pause && pad_on();
        const bool pad_previewing = !running_view && face == Face::Input &&
                                    pad_preview_size.x > 0;
        if (!running_view) pad_editing = false;
        pad.set_editing(pad_editing || pad_previewing);
        const ImVec2 win_sz((float)win_w, (float)win_h);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) quit = true;
            if (ev.type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
                save_bram();
                ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
            }
            if (demo_session && ev.type == SDL_EVENT_KEY_DOWN &&
                (ev.key.scancode == SDL_SCANCODE_ESCAPE ||
                 ev.key.scancode == SDL_SCANCODE_AC_BACK)) {
                return_to_wizard();
                continue;
            }

            /* ---- the on-screen pad, before the gun and the mouse ----
             * A finger on a drawn button is a button, not a shot; anything
             * the pad does not claim falls through to the pointer path. */
            if (pad_active) {
                if (pad.handle(ev, win_sz, ImVec2(0, 0), win_sz, pad_sink)) continue;
            } else if (pad_previewing) {
                if (pad.handle(ev, win_sz, pad_preview_pos, pad_preview_size, pad_silent))
                    continue;
            }

            /*
             * Super+Q closes, Super+M resizes.
             /* Back / Escape exits the full-screen pad preview. Added
             * 2026-09-18 because the preview has no chrome and the
             * user needs a keyboard-style way out. */
            if (ev.type == SDL_EVENT_KEY_DOWN && !running_view &&
                face == Face::Input && g_pad_tab == 1 &&
                (ev.key.scancode == SDL_SCANCODE_ESCAPE ||
                 ev.key.scancode == SDL_SCANCODE_AC_BACK)) {
                g_pad_tab = 0; g_pad_fullscreen = 0; continue;
            }

            /*
             * Super+Q closes, Super+M resizes.
             *
             * Handled before anything else so they work from every screen,
             * including with a game running and the pause menu up -- a
             * shortcut that only works on the shelf is a shortcut nobody
             * trusts. Super rather than Ctrl because Ctrl belongs to the
             * guest: a Saturn game may well want it.
             */
            if (ev.type == SDL_EVENT_KEY_DOWN && (ev.key.mod & SDL_KMOD_GUI)) {
                if (ev.key.scancode == SDL_SCANCODE_Q) { quit = true; continue; }
                if (ev.key.scancode == SDL_SCANCODE_M) {
                    /* Between filling the display and a window you can put
                     * beside something else. Not SDL_MaximizeWindow: on a
                     * tiling desktop that does nothing visible, and the point
                     * is to change size. */
                    static bool big = false;
                    big = !big;
                    if (big) {
                        const SDL_DisplayID d = SDL_GetDisplayForWindow(win);
                        SDL_Rect r{};
                        if (SDL_GetDisplayUsableBounds(d, &r) && r.w > 0)
                            SDL_SetWindowSize(win, (int)(r.w * 0.95f),
                                              (int)(r.h * 0.95f));
                        else
                            SDL_MaximizeWindow(win);
                    } else {
                        SDL_RestoreWindow(win);
                        SDL_SetWindowSize(win, 1280, 800);
                    }
                    SDL_SyncWindow(win);
                    continue;
                }
            }

            if (running_view && !show_pause &&
                (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP)) {
                const bool down = ev.type == SDL_EVENT_KEY_DOWN;
                if (down && ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                    show_pause = true;
                } else if (down && ev.key.scancode == SDL_SCANCODE_F5) {
                    do_save_state();
                } else if (down && ev.key.scancode == SDL_SCANCODE_F8) {
                    do_load_state();
                } else {
                    for (const KeyBind &b : kKeyMap)
                        if (b.key == ev.key.scancode)
                            pad_press(ymir, 1, b.button, kSrcButton, down);
                }
            } else if (running_view && show_pause && ev.type == SDL_EVENT_KEY_DOWN &&
                       ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                show_pause = false;
            }

            /* ---- the pointer: mouse and finger, one path ---- */
            if (running_view && !show_pause) {
                switch (ev.type) {
                case SDL_EVENT_MOUSE_MOTION:
                    pointer.x = ev.motion.x; pointer.y = ev.motion.y;
                    pointer.seen = true;
                    mouse_dx += (int)ev.motion.xrel;
                    mouse_dy += (int)ev.motion.yrel;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    const bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    pointer.x = ev.button.x; pointer.y = ev.button.y;
                    pointer.seen = true;
                    if (ev.button.button == SDL_BUTTON_LEFT)   pointer.trigger = down;
                    if (ev.button.button == SDL_BUTTON_RIGHT)  pointer.reload  = down;
                    if (ev.button.button == SDL_BUTTON_MIDDLE) pointer.start   = down;
                    break;
                }
                /* A finger IS the trigger: there is nowhere to rest a touch
                 * without meaning to shoot, so touching aims and fires. */
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_FINGER_UP:
                    pointer.x = ev.tfinger.x * (float)win_w;
                    pointer.y = ev.tfinger.y * (float)win_h;
                    pointer.seen = true;
                    pointer.trigger = ev.type != SDL_EVENT_FINGER_UP;
                    break;
                default: break;
                }
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
                                pad_press(ymir, port, b.button, kSrcButton, down);
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
                            pad_press(ymir, port, kTriggerButton[left ? 0 : 1],
                                      kSrcTrigger, now);
                        }
                    }
                }
            }
        }

        if (lifecycle.take_pause_request()) {
            if (running_view) show_pause = true;
            pad.release_all(pad_sink);
            save_bram();
            ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
        }
        if (lifecycle.suspended()) {
            SDL_Delay(20);
            continue;
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
                        /* Through the same source mask as the D-pad, so
                         * steering with the stick while the D-pad is also
                         * being used does not have one release cancel the
                         * other. */
                        pad_press(ymir, port, dirs[d], kSrcStick, want[d]);
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

            /* ---- the pointing devices ---- */
            if (const int gp = port_with(saturn::Peripheral::VirtuaGun)) {
                /* Window pixels back to Saturn pixels, through the same
                 * rectangle the picture was drawn into. */
                const PictureRect r = picture_rect(win_w, win_h, frame_w, frame_h,
                                                   cfg.machine);
                int gx = 0, gy = 0;
                if (r.w > 0 && r.h > 0 && frame_w > 0 && frame_h > 0) {
                    gx = (int)((pointer.x - r.x) / r.w * (float)frame_w);
                    gy = (int)((pointer.y - r.y) / r.h * (float)frame_h);
                    gx = std::clamp(gx, 0, frame_w - 1);
                    gy = std::clamp(gy, 0, frame_h - 1);
                }
                ymir_bridge_set_virtua_gun_fb_size(ymir, frame_w, frame_h);
                ymir_bridge_set_virtua_gun_state(ymir, gp, gx, gy,
                                                 pointer.trigger, pointer.start,
                                                 pointer.reload);
            }
            if (const int mp = port_with(saturn::Peripheral::ShuttleMouse)) {
                if (mouse_dx || mouse_dy) {
                    ymir_bridge_set_mouse_motion(ymir, mp, mouse_dx, mouse_dy);
                    mouse_dx = mouse_dy = 0;
                }
                ymir_bridge_set_mouse_button(ymir, mp, YMIR_MOUSE_LEFT,  pointer.trigger);
                ymir_bridge_set_mouse_button(ymir, mp, YMIR_MOUSE_RIGHT, pointer.reload);
                ymir_bridge_set_mouse_button(ymir, mp, YMIR_MOUSE_MIDDLE, pointer.start);
            }
        }

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
                    /* The scale mode is chosen per frame, from how the
                     * picture actually fits the window -- see the blit. */
                    frame_w = w; frame_h = h;
                }
                if (frame) SDL_UpdateTexture(frame, nullptr, pix, w * 4);
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        library_drag_item = false;
        if (ImGui::IsMouseClicked(0)) library_dragged = false;

        if (const std::string chosen = saturn::saf_take_picked(); !chosen.empty()) {
            save_bram();
            adopt_tree(chosen);
            resolve_saves();
            load_bram();
            rescan();
            adopt_bios_from_discs();
            saturn::save_app_config(cfg_path, cfg);
        }

        /* ================= first run ================= */
        if (wizard) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
            ImGui::Begin("##setup", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            /* Discs before BIOS, deliberately. The BIOS now lives in a
             * folder the user chooses, so asking for it first left "look for
             * it" with nowhere to look. */
            static const char *kStep[] = { "Welcome", "Your discs", "The BIOS",
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
                TextDimWrapped("The emulation is Ymir's work, under the GPL. Supply your own "
                               "BIOS and discs; the setup wizard includes a homebrew demo.");
            }

            else if (wstep == 1) {
                ImGui::TextWrapped("Choose the parent folder for your Saturn collection.");
                TextDimWrapped("It contains bios, games, artwork and saves. Existing folders and flat disc libraries are supported.");
                TextDimWrapped("For artwork, use a PNG or JPEG with the same name as the disc, for example Daytona USA.chd and artwork/Daytona USA.png.");
                ImGui::Spacing();
#if defined(__ANDROID__)
                /*
                 * The grant first, and nothing above it.
                 *
                 * This used to open with two paragraphs about Android's
                 * storage rules and a pair of the app's own folders, and the
                 * button that most people actually want was under all of it,
                 * off the bottom of a phone screen. The explanation is still
                 * here; it is just no longer in the way.
                 */
                ImGui::TextWrapped("Choose one folder and this app will use it for "
                                   "everything -- your discs, your BIOS and your "
                                   "saves.");
                ImGui::Spacing();
                if (saturn::saf_available()) {
                    if (ImGui::Button("Choose a folder...", ImVec2(fs2 * 14.0f, 0)))
                        saturn::saf_pick();
                    ImGui::Spacing();
                    TextDimWrapped("Android will ask you to grant it. Inside it this "
                                   "app makes four folders and touches nothing else: "
                                   "bios for your Saturn BIOS, games for your discs, artwork "
                                   "for your pictures, and saves for progress. Folders already there "
                                   "are left exactly as they are.");
                    ImGui::Spacing();

                    /* The grant arrives asynchronously, so this is how it is
                     * noticed -- there is nothing else to wait on. */
                    for (const saturn::SafTree &t : saturn::saf_trees()) {
                        const bool in_use =
                            t.uri == cfg.disc_tree ||
                            (!t.path.empty() && !cfg.disc_root.empty() &&
                             cfg.disc_root.rfind(t.path, 0) == 0);
                        ImGui::PushID(t.uri.c_str());
                        if (in_use) {
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                            ImGui::TextWrapped("Using %s", t.name.c_str());
                            ImGui::PopStyleColor();
                        } else if (ImGui::Button(t.name.c_str())) {
                            save_bram();
                            adopt_tree(t.uri);
                            resolve_saves();
                            load_bram();
                            rescan();
                            adopt_bios_from_discs();
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                TextDimWrapped("Or use one of the app's own folders, which need no "
                               "permission at all and which a file manager or a USB "
                               "cable can reach. There is one per storage volume, so "
                               "a memory card is here too:");
                ImGui::Spacing();
                for (const std::string &r : saturn::candidate_disc_roots()) {
                    ImGui::PushID(r.c_str());
                    if (ImGui::RadioButton("##r", cfg.disc_tree.empty() &&
                                                  cfg.parent_root == r)) {
                        save_bram();
                        saturn::configure_parent_folder(cfg, r);
                        resolve_saves();
                        load_bram();
                        rescan();
                        adopt_bios_from_discs();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", r.c_str());
                    ImGui::PopID();
                }
#else

                ImGui::Spacing();
                for (const std::string &r : saturn::candidate_disc_roots()) {
                    ImGui::PushID(r.c_str());
                    if (ImGui::RadioButton("##r", cfg.parent_root == r)) {
                        save_bram();
                        saturn::configure_parent_folder(cfg, r);
                        resolve_saves();
                        load_bram();
                        rescan();
                        adopt_bios_from_discs();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", r.c_str());
                    ImGui::PopID();
                }
                ImGui::Spacing();
                ImGui::BeginDisabled(saturn::pick_in_progress());
                if (ImGui::Button("Choose another folder...")) saturn::begin_pick_folder(cfg.parent_root);
                ImGui::EndDisabled();
                {
                    std::string got;
                    if (saturn::take_pick(got)) {
                        save_bram();
                        saturn::configure_parent_folder(cfg, got);
                        resolve_saves();
                        load_bram();
                        rescan();
                        adopt_bios_from_discs();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                }
#endif
                ImGui::Spacing();
                TextDimWrappedF("%zu disc%s found so far.", games.size(),
                                games.size() == 1 ? "" : "s");
            }

            else if (wstep == 2) {
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
                if (saturn::pickers_usable()) {
                    ImGui::BeginDisabled(saturn::pick_in_progress());
                    if (ImGui::Button("Choose the BIOS file...")) saturn::begin_pick_file(cfg.parent_root.empty() ? cfg.disc_root : saturn::folder_for(cfg.parent_root, "bios"));
                    ImGui::EndDisabled();
                } else {
                    /*
                     * No picker worth offering here -- see pickers_usable().
                     * Finding it is the route instead, and a better one: the
                     * BIOS goes in the folder chosen on the step before, and
                     * the app notices.
                     */
                    if (!cfg.disc_tree.empty() || !cfg.disc_root.empty()) {
                        ImGui::TextWrapped("Put it in the bios folder inside the "
                                           "folder you chose, and it will be found: a "
                                           "Saturn BIOS is exactly 512 KB, which "
                                           "nothing else there is.");
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                        ImGui::TextWrapped("Go back a step and choose a folder first -- "
                                           "the BIOS goes inside it, and until there is "
                                           "one there is nowhere to look.");
                        ImGui::PopStyleColor();
                    }
                    ImGui::Spacing();
                    ImGui::BeginDisabled(cfg.disc_tree.empty() && cfg.disc_root.empty());
                    if (ImGui::Button("Look for it now", ImVec2(fs2 * 12.0f, 0)))
                        adopt_bios_from_discs();
                    ImGui::EndDisabled();
                }
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

            else if (wstep == kWizardDemoStep) {
                const std::string demo = saturn::demo_disc_path();
                ImGui::Spacing();
                if (demo.empty()) {
                    ImGui::TextWrapped("The bundled demo files are unavailable. "
                                       "You can continue setup and use your own discs.");
                } else {
                    ImGui::TextWrapped("A demo is included, so there is something to "
                                       "run before you have copied anything across.");
                    ImGui::Spacing();
                    ImGui::TextUnformatted(saturn::demo_title());
                    if (demo_tested) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.55f, 1.0f),
                                           "Demo finished. Continue the wizard or run it again.");
                    }
                    TextDimWrapped("Free Saturn homebrew from the SegaXtreme "
                                   "competition. Not a Sega game, and not ours "
                                   "either -- it is included with permission as "
                                   "something to test with.");
                    ImGui::Spacing();
                    ImGui::BeginDisabled(!bios_loaded);
                    if (ImGui::Button("Run the demo", ImVec2(fs2 * 12.0f, 0))) {
                        demo_error.clear();
                        save_bram();
                        SDL_CreateDirectory(demo_saves_dir.c_str());
                        const int32_t preserved = ymir_bridge_save_internal_backup_memory(
                            ymir, demo_restore_path.c_str());
                        if (!path_is_file(demo_bram_path))
                            ymir_bridge_save_internal_backup_memory(ymir, demo_bram_path.c_str());
                        if (preserved == YMIR_OK &&
                            ymir_bridge_load_internal_backup_memory(ymir, demo_bram_path.c_str(), 0) == YMIR_OK) {
                            demo_session = true;
                            states_dir = demo_saves_dir + "/states";
                            SDL_CreateDirectory(states_dir.c_str());
                            rescan();
                            loaded_game_index = -1;
                            insert_disc(0, 0);
                            if (loaded_game_index == 0) {
                                wizard = false;
                                show_pause = false;
                                running_view = true;
                            } else {
                                return_to_wizard();
                                demo_tested = false;
                                demo_error = message;
                            }
                        } else {
                            demo_error = "Could not prepare the demo saves. Check that app storage is writable.";
                        }
                    }
                    ImGui::EndDisabled();
                    TextDimWrapped("Return here using the button beside the demo, Escape, or Android Back.");
                    if (!demo_error.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f), "%s", demo_error.c_str());
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
                ImGui::Text("Artwork: %s", cfg.artwork_dir.c_str());
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
            /*
             * A rail down the left and the machine across the top of the rest.
             *
             * The name and the way around the application belong together and
             * they belong out of the way: a quarter of the width, once, rather
             * than a band of buttons eating into every screen underneath. That
             * leaves the whole top of the working area for the Saturn itself,
             * which is what somebody came to look at.
             */
            /*
             * Phone, tablet or desktop -- decided from the window, not from
             * the platform.
             *
             * A phone in landscape has a tablet's proportions and a desktop
             * window dragged narrow has a phone's, and in both cases what the
             * layout should do is the same. Measuring the window covers every
             * device without a list of them, and it means the thing can be
             * checked on a desktop by making the window small.
             *
             * The unit is the text size rather than pixels, so this holds on a
             * 400ppi handheld and a 1080p monitor alike.
             */
            const float em = ImGui::GetFontSize();
            const bool narrow  = cw < em * 34.0f;   /* a phone, near enough */
            const bool two_col = cw > em * 46.0f;   /* room for two columns  */
            /*
             * Height matters as much as width, and separately.
             *
             * A phone turned on its side is wide enough for the rail and far
             * too short for a tall header and a fat ports strip -- it is the
             * one shape where getting this wrong leaves nothing between them
             * for the actual list. So the two are asked separately rather than
             * one "is it a phone" flag deciding both.
             */
            const bool shortscr = (float)win_h < em * 30.0f;

            const float port_art_h = ImGui::GetFrameHeight() * (shortscr ? 1.0f : 1.6f);
            const float ports_row  = std::max(ImGui::GetFrameHeightWithSpacing(),
                                              port_art_h + ImGui::GetStyle().ItemSpacing.y);
            const float ports_h = ports_row + ImGui::GetStyle().WindowPadding.y * 3.0f;
            const float body_h  = 0.0f;   /* the sockets are in the header now */
            bool  ports_in_header = false;
            float ports_want = 0.0f;

            /* Big enough for the machine to be a photograph of a machine
             * rather than an icon of one, and never so big that the shelf
             * beneath it has nowhere to go. */
            /* At least what the panel actually holds -- three lines and a row
             * of buttons -- and then as much of the window as it is worth
             * giving a photograph. A fraction of the height alone was fine
             * until the buttons grew for touch, at which point the panel
             * sprouted a scrollbar on the one screen that should never need
             * one. */
            const float head_need = ImGui::GetTextLineHeightWithSpacing() * 3.0f
                                  + ImGui::GetFrameHeightWithSpacing()
                                  + ImGui::GetStyle().WindowPadding.y * 2.0f;
            const float head_h = std::max(head_need,
                                          std::min(fs * 9.5f,
                                                   (float)win_h * (narrow ? 0.20f : 0.26f)));
            /* A short screen has no spare rows for a picture of a console. */
            const bool show_console_art = art_console && art_console_h > 0 &&
                                          !narrow && !shortscr;
            const float rail_w = std::max(cw * 0.22f, fs * 8.5f);

            auto tab = [&](const char *label, Face f, const ImVec2 &size) {
                const bool on = (face == f);
                if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(label, size)) face = f;
                if (on) ImGui::PopStyleColor();
            };
            /* Only the narrow layout uses this, to decide how many buttons
             * fit on a row before it wraps. */
            const int n_tabs = 11;

            /* ============ the rail, where there is width for one ============ */
            if (!narrow && !g_pad_fullscreen) {
                ImGui::BeginChild("##rail", ImVec2(rail_w, body_h), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
                if (logo && logo_w > 0) {
                    const float w = ImGui::GetContentRegionAvail().x;
                    ImGui::Image((ImTextureID)(intptr_t)logo,
                                 ImVec2(w, w * (float)logo_h / (float)logo_w));
                } else {
                    ImGui::TextUnformatted("RETRO-SATURN");
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                const ImVec2 bsz(-FLT_MIN, fs * 2.0f);
                /*
                 * Ordered by how often it is wanted, not by what it is.
                 *
                 * Launch is what somebody opened the application to do, so it
                 * is first. Then the things you go to during an evening --
                 * another game, another download, a save. Then the console's
                 * own settings under a heading, because five of them in a row
                 * with no label is a list of unrelated words. Setup last:
                 * where the BIOS and the folders live, which is a thing you do
                 * once.
                 */
                /*
                 * The whole rail, always, without a scrollbar.
                 *
                 * A navigation you have to scroll to see is not a navigation,
                 * and on a handheld -- where the buttons are twice the size
                 * for a thumb -- eleven of them did not fit. So the height is
                 * worked out from what is left rather than fixed, and if that
                 * would make them too small to hit, they go into two columns
                 * instead. Nothing is ever hidden.
                 */
                /* A short name for the two-column case: half a rail is not
                 * wide enough for "Save states", and a clipped label is worse
                 * than a terse one. */
                struct Entry { const char *label; const char *terse; Face face; bool heading; };
                Entry entries[] = {
                    { "Launch",      "Launch", Face::Launch,    false },
                    { "Discs",       "Discs",  Face::Discs,     false },
                    { "Save states", "Saves",  Face::Saves,     false },
                    { "MACHINE",     "MACHINE",Face::Memory,    true  },
                    { "Memory",      "Memory", Face::Memory,    false },
                    { "Picture",     "Picture",Face::Picture,   false },
                    { "Touch pad",   "Pad",    Face::Input,     false },
                    { "Sound",       "Sound",  Face::Sound,     false },
                    { "Processor",   "CPU",    Face::Processor, false },
                    { "Disc drive",  "Drive",  Face::Drive,     false },
                    { "About",       "About",  Face::About,     false },
                    { "Paths",       "Paths",  Face::Paths,     false },
                    { "Setup",       "Setup",  Face::Setup,     false },
                };
                int n_rows = 0, n_head = 0;
                for (const Entry &e : entries) {
                    if (e.heading) n_head++; else n_rows++;
                }

                const float avail_h = ImGui::GetContentRegionAvail().y;
                const float sp_y = ImGui::GetStyle().ItemSpacing.y;
                const float head_row = ImGui::GetTextLineHeightWithSpacing();
                const float want = fs * 2.0f;          /* the comfortable size  */
                const float floor_h = fs * 1.45f;      /* the smallest thumb    */

                auto fits = [&](int cols, float bh) {
                    const int rows = (n_rows + cols - 1) / cols;
                    return rows * (bh + sp_y) + n_head * head_row <= avail_h;
                };

                int cols = 1;
                float bh = want;
                if (!fits(1, want)) {
                    /* Shrink first: one column reads better than two. */
                    const int rows = n_rows;
                    bh = (avail_h - n_head * head_row) / (float)rows - sp_y;
                    if (bh < floor_h) { cols = 2; bh = want;
                        if (!fits(2, want)) {
                            const int r2 = (n_rows + 1) / 2;
                            bh = std::max(floor_h,
                                          (avail_h - n_head * head_row) / (float)r2 - sp_y);
                        }
                    }
                }

                /*
                 * Then grow into whatever is left.
                 *
                 * The size above is the SMALLEST that fits, and on a wide
                 * screen in two columns that left a third of the rail empty
                 * below Setup -- a column of buttons floating in a panel. The
                 * bar is a fixed shape, so the buttons should end where it
                 * does. Capped at three lines, because past that they stop
                 * reading as buttons and start reading as panels.
                 */
                {
                    const int rows = (n_rows + cols - 1) / cols;
                    const float room = (avail_h - n_head * head_row) / (float)rows - sp_y;
                    bh = std::min(std::max(bh, room), fs * 3.0f);
                }

                const float col_w = cols == 1
                    ? -FLT_MIN
                    : (ImGui::GetContentRegionAvail().x -
                       ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const ImVec2 esz(col_w, bh);

                int placed = 0;
                for (const Entry &e : entries) {
                    if (e.heading) {
                        if (cols == 2 && (placed % 2) == 1) { placed++; }  /* finish the row */
                        TextDim("  %s", e.label);
                        continue;
                    }
                    if (cols == 2 && (placed % 2) == 1) ImGui::SameLine();
                    tab(cols == 2 ? e.terse : e.label, e.face, esz);
                    placed++;
                }
                ImGui::EndChild();
                ImGui::SameLine();
            }
            ImGui::BeginChild("##right", ImVec2(0, body_h));

            /* The sockets live in the header now, beside the machine they
             * belong to -- they are part of the console, not a status bar. */
            /* Wide enough for two arrows, a photograph and the longest name
             * in the list, measured rather than guessed -- at eleven ems
             * "Control Pad" came out as "Control Pa" and one arrow fell off
             * the end. */
            const float half = std::max(ImGui::CalcTextSize("3D Control Pad").x +
                                        ImGui::GetFrameHeight() * 2.0f +
                                        port_art_h * 1.6f +
                                        ImGui::GetStyle().ItemSpacing.x * 4.0f,
                                        ImGui::GetFontSize() * 12.0f);
        /*
             * A socket, with arrows rather than a drop-down.
             *
             * There are seven peripherals and the list never grows while
             * you look at it, so stepping through them is quicker than
             * opening a menu, reading it and picking -- and on a handheld
             * it is a shoulder button rather than a pointer. The caption
             * is gone: the picture says what is plugged in better than the
             * words "PORT 1" ever did, and left is the left socket.
             */
            auto port = [&](const char *id, saturn::Peripheral &p, float box_h) {
                ImGui::BeginChild((std::string("##box") + id).c_str(),
                                  ImVec2(half, box_h));
                const int last = (int)saturn::Peripheral::ShuttleMouse;
                auto step = [&](int by) {
                    int v = ((int)p + by + (last + 1)) % (last + 1);
                    p = (saturn::Peripheral)v;
                    apply_ports();
                    saturn::save_app_config(cfg_path, cfg);
                };

                const float arrow = ImGui::GetFrameHeight();
                /* The row is the box when the box says how tall it is -- the
                 * header stacks two of these in the space one used to have. */
                const float row = box_h > 0.0f
                                ? std::min(port_art_h,
                                           box_h - ImGui::GetStyle().WindowPadding.y * 2.0f)
                                : port_art_h;
                const float top = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(top + (row - arrow) * 0.5f);
                ImGui::PushID(id);
                if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) step(-1);
                ImGui::SameLine();

                int aw = 0, ah = 0;
                float used = arrow * 2.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
                /* Same bargain as the header: on a narrow screen the name
                 * of the device matters more than a picture of it, and
                 * both together left "Control Pad" reading "Control". */
                SDL_Texture *ptex = (narrow || shortscr) ? nullptr
                                                         : art_for(p, &aw, &ah);
                if (SDL_Texture *tex = ptex) {
                    const float w = row * (float)aw / (float)ah;
                    ImGui::SetCursorPosY(top);
                    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, row));
                    ImGui::SameLine();
                    used += w + ImGui::GetStyle().ItemSpacing.x;
                }

                /* The name, centred in whatever is left between the
                 * arrows, so it does not jump about as the word changes
                 * length. */
                const float name_w = std::max(ImGui::GetFontSize() * 4.0f,
                                              half - used);
                const char *name = saturn::peripheral_name(p);
                const float tw = ImGui::CalcTextSize(name).x;
                const float here = ImGui::GetCursorPosX();
                ImGui::SetCursorPosY(top + (row - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::SetCursorPosX(here + std::max(0.0f, (name_w - tw) * 0.5f));
                ImGui::TextUnformatted(name);

                ImGui::SameLine();
                ImGui::SetCursorPosX(here + name_w);
                ImGui::SetCursorPosY(top + (row - arrow) * 0.5f);
                if (ImGui::ArrowButton("##next", ImGuiDir_Right)) step(+1);
                ImGui::PopID();
                ImGui::EndChild();
            };

            /* ============ the machine, and what is in it ============ */
            /*
             * The machine is a page, not a banner.
             *
             * It used to sit above every screen, which meant a third of the
             * window was given to the disc in the drive while you were
             * adjusting the sound. It is the first entry in the rail now, and
             * the settings pages get the room back.
             */
            if (face == Face::Launch) {
            ImGui::BeginChild("##drive", ImVec2(0, 0), ImGuiChildFlags_Borders);
            {
                /*
                 * The whole page is the machine.
                 *
                 * Everything here is sized from what is left rather than by
                 * hand, so the same arrangement fills a phone in landscape and
                 * a desktop window: the console takes most of the height, the
                 * two peripherals sit under its ports in the order they are on
                 * the front of the real one, and the disc turns beside it.
                 */
                const ImVec2 start = ImGui::GetCursorPos();
                const ImVec2 avail = ImGui::GetContentRegionAvail();

                /*
                 * A footer pinned to the bottom, and the machine gets the
                 * rest.
                 *
                 * The page used to be drawn top-down and stop wherever it ran
                 * out of content, which left a third of the screen empty
                 * underneath and the button stranded on the left. Reserving
                 * the footer first and measuring the stage against what is
                 * left means the machine grows into the whole page on a big
                 * window and the button is always in the same corner.
                 */
                const float btn_w = fs * 11.0f, btn_h = fs * 2.2f;
                const float footer_h = btn_h + ImGui::GetStyle().ItemSpacing.y * 2.0f;
                const float stage_h = std::max(avail.y - footer_h, fs * 6.0f);

                /* The console, as big as the stage allows, with room beside it
                 * for the disc. */
                float ch = stage_h * 0.62f;
                float cwid = art_console_h > 0
                           ? ch * (float)art_console_w / (float)art_console_h : ch;
                const float disc_d = std::min(stage_h * 0.55f, avail.x * 0.28f);
                const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
                if (cwid + gap + disc_d > avail.x) {
                    cwid = std::max(avail.x - gap - disc_d, avail.x * 0.35f);
                    ch = art_console_h > 0
                       ? cwid * (float)art_console_h / (float)art_console_w : cwid;
                }

                /* Centred as a pair, so the machine sits in the middle of its
                 * page rather than jammed against the left edge. */
                const float group_w = cwid + gap + disc_d;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     std::max(0.0f, (avail.x - group_w) * 0.5f));

                ImGui::BeginGroup();
                if (art_console && art_console_h > 0)
                    ImGui::Image((ImTextureID)(intptr_t)art_console, ImVec2(cwid, ch));
                else
                    ImGui::Dummy(ImVec2(cwid, ch));

                /*
                 * Under the console, under its ports -- and changeable there.
                 *
                 * What is plugged in is part of the picture of the machine, so
                 * the place it is shown is the place to change it. Arrows
                 * rather than a menu: there are seven peripherals, the list
                 * never grows while you look at it, and stepping is quicker
                 * than opening something and reading it.
                 */
                const float sel_h = ImGui::GetFrameHeightWithSpacing();
                const float half_w = (cwid - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const bool wrap_port_label = ImGui::CalcTextSize("Controller port 1").x > half_w;
                const float label_h = ImGui::GetTextLineHeightWithSpacing() * (wrap_port_label ? 2.0f : 1.0f);
                const float ph = std::max(stage_h - ch - sel_h - label_h
                                              - ImGui::GetStyle().ItemSpacing.y,
                                          ImGui::GetFrameHeight() * 0.8f);
                for (int which = 1; which <= 2; ++which) {
                    saturn::Peripheral &p = (which == 1) ? cfg.machine.port1
                                                         : cfg.machine.port2;
                    if (which == 2) ImGui::SameLine();
                    ImGui::BeginGroup();
                    char port_label[40];
                    snprintf(port_label, sizeof port_label,
                             wrap_port_label ? "Controller\nport %d" : "Controller port %d", which);
                    const ImVec2 label_at = ImGui::GetCursorScreenPos();
                    const ImVec2 label_size = ImGui::CalcTextSize(port_label);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(label_at.x + std::max(0.0f, (half_w - label_size.x) * 0.5f), label_at.y),
                        ImGui::GetColorU32(ImGuiCol_Text), port_label);
                    ImGui::Dummy(ImVec2(half_w, label_h - ImGui::GetStyle().ItemSpacing.y));
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(half_w, ph));
                    int aw = 0, ah = 0;
                    SDL_Texture *tex = art_for(p, &aw, &ah);
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (tex && ah > 0) {
                        float ih = ph, iw = ih * (float)aw / (float)ah;
                        if (iw > half_w) { iw = half_w; ih = iw * (float)ah / (float)aw; }
                        dl->AddImage((ImTextureID)(intptr_t)tex,
                                     ImVec2(at.x + (half_w - iw) * 0.5f,
                                            at.y + (ph - ih) * 0.5f),
                                     ImVec2(at.x + (half_w + iw) * 0.5f,
                                            at.y + (ph + ih) * 0.5f));
                    } else if (p == saturn::Peripheral::None) {
                        /* An empty socket, drawn as one: a dark slot of the
                         * shape the Saturn's actually are. */
                        const float sw = std::min(half_w * 0.5f, ph * 1.7f);
                        const float sh = std::min(ph * 0.42f, sw * 0.34f);
                        const ImVec2 a(at.x + (half_w - sw) * 0.5f,
                                       at.y + (ph - sh) * 0.5f);
                        const ImVec2 b(a.x + sw, a.y + sh);
                        dl->AddRectFilled(a, b, IM_COL32(10, 11, 16, 255), 3.0f);
                        dl->AddRect(a, b, IM_COL32(64, 72, 94, 220), 3.0f);
                    } else {
                        /* Plugged in, but we have no photograph of this one.
                         * A plate with its name says so; borrowing another
                         * peripheral's picture would be a lie. */
                        const float pw = half_w * 0.8f, phh = ph * 0.8f;
                        const ImVec2 a(at.x + (half_w - pw) * 0.5f,
                                       at.y + (ph - phh) * 0.5f);
                        const ImVec2 b(a.x + pw, a.y + phh);
                        dl->AddRectFilled(a, b, IM_COL32(24, 27, 38, 255), 5.0f);
                        dl->AddRect(a, b, IM_COL32(80, 90, 116, 220), 5.0f);
                        const char *nm2 = saturn::peripheral_name(p);
                        const ImVec2 tsz = ImGui::CalcTextSize(nm2);
                        dl->AddText(ImVec2(a.x + (pw - tsz.x) * 0.5f,
                                           a.y + (phh - tsz.y) * 0.5f),
                                    IM_COL32(180, 190, 210, 255), nm2);
                    }

                    /* ‹ name › under the socket it belongs to. */
                    ImGui::PushID(which);
                    const int last = (int)saturn::Peripheral::ShuttleMouse;
                    auto step = [&](int by) {
                        p = (saturn::Peripheral)(((int)p + by + last + 1) % (last + 1));
                        apply_ports();
                        saturn::save_app_config(cfg_path, cfg);
                    };
                    /*
                     * Laid out by measuring the row and centring the whole of
                     * it, not by placing each piece at a computed column.
                     *
                     * The column version put the second arrow at a fixed
                     * offset and the name at another, and on a handheld -- for
                     * which the arrows are twice the size -- the name ran
                     * underneath the arrow. Natural widths cannot overlap.
                     */
                    const char *nm = saturn::peripheral_name(p);
                    const float arrow = ImGui::GetFrameHeight();
                    const float sp = ImGui::GetStyle().ItemSpacing.x;
                    const float row_w = arrow * 2.0f + sp * 2.0f +
                                        ImGui::CalcTextSize(nm).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                         std::max(0.0f, (half_w - row_w) * 0.5f));
                    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) step(-1);
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(nm);
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) step(+1);
                    ImGui::PopID();
                    ImGui::EndGroup();
                }
                ImGui::EndGroup();

                /* ---- the disc ---- */
                /*
                 * Always there, whether the tray is full or not. An empty
                 * drive that draws nothing leaves a hole beside the console
                 * and the whole tableau shifts sideways the moment a game is
                 * chosen; a blank disc says "empty" without moving anything.
                 */
                {
                    ImGui::SameLine(0.0f, gap);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(disc_d, stage_h));
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    const ImVec2 c(at.x + disc_d * 0.5f, at.y + stage_h * 0.5f);
                    const float r = disc_d * 0.5f;

                    /*
                     * Still, not spinning.
                     *
                     * It turned, which drew the eye to the one thing on the
                     * page that is not a control and away from the game you
                     * came here to start. A disc at rest is also what the
                     * drive is actually doing: nothing, until you press
                     * Launch.
                     */
                    SDL_Texture *disc_tex = nullptr;
                    if (disc_tex) {
                        dl->AddImage((ImTextureID)(intptr_t)disc_tex,
                                     ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
                        dl->AddCircle(c, r * 0.99f, IM_COL32(150, 170, 200, 70), 64, 1.5f);
                    } else {
                        /*
                         * A blank disc, at rest.
                         *
                         * The sheen used to be twelve full-strength coloured
                         * spokes, which worked only because it was spinning:
                         * turning, they blurred into iridescence; stopped,
                         * they are a pie chart. So the colour is now a faint
                         * sweep across one side, the way light actually
                         * catches a CD lying on a desk, and the rest is the
                         * disc itself -- body, data ring, clamping ring, hub.
                         */
                        dl->AddCircleFilled(c, r, IM_COL32(28, 32, 44, 255), 64);

                        /*
                         * The sheen, as one band rather than a fan of lines.
                         *
                         * Drawn line by line it came out as visible spokes --
                         * each one a hard-edged quad, and no number of them
                         * blends, because where they overlap the alpha piles
                         * up and where they do not there is a gap. So the arc
                         * is built as a triangle strip with the colour on the
                         * vertices, which is what makes it a gradient: hue
                         * turning along the sweep and the alpha falling to
                         * nothing at both ends, so it fades into the disc
                         * instead of stopping at an edge.
                         */
                        {
                            const float from  = -2.6f;   /* upper left */
                            const float sweep = 2.3f;
                            const float r0 = r * 0.34f, r1 = r * 0.97f;
                            const int   seg = 48;

                            dl->PrimReserve(seg * 6, (seg + 1) * 2);
                            const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
                            const ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
                            for (int i = 0; i <= seg; ++i) {
                                const float t = (float)i / (float)seg;
                                const float a = from + sweep * t;
                                float cr, cg, cb;
                                ImGui::ColorConvertHSVtoRGB(0.52f + 0.38f * t, 0.5f, 1.0f,
                                                            cr, cg, cb);
                                const float fade = sinf(t * 3.14159265f);
                                const ImU32 cin = IM_COL32((int)(cr * 255), (int)(cg * 255),
                                                           (int)(cb * 255), (int)(26 * fade));
                                const ImU32 cout = IM_COL32((int)(cr * 255), (int)(cg * 255),
                                                            (int)(cb * 255), (int)(46 * fade));
                                dl->PrimWriteVtx(ImVec2(c.x + cosf(a) * r0,
                                                        c.y + sinf(a) * r0), uv, cin);
                                dl->PrimWriteVtx(ImVec2(c.x + cosf(a) * r1,
                                                        c.y + sinf(a) * r1), uv, cout);
                            }
                            for (int i = 0; i < seg; ++i) {
                                const ImDrawIdx q = base + (ImDrawIdx)(i * 2);
                                dl->PrimWriteIdx(q);     dl->PrimWriteIdx(q + 1);
                                dl->PrimWriteIdx(q + 3);
                                dl->PrimWriteIdx(q);     dl->PrimWriteIdx(q + 3);
                                dl->PrimWriteIdx(q + 2);
                            }
                        }

                        /* The rings: a disc is read off concentric tracks and
                         * looks like it. */
                        dl->AddCircle(c, r * 0.985f, IM_COL32(150, 170, 200, 90), 64, 1.5f);
                        dl->AddCircle(c, r * 0.62f, IM_COL32(120, 140, 175, 28), 64, 1.0f);
                        dl->AddCircleFilled(c, r * 0.30f, IM_COL32(18, 21, 30, 255), 48);
                        dl->AddCircle(c, r * 0.30f, IM_COL32(150, 170, 200, 110), 48, 1.5f);
                        dl->AddCircle(c, r * 0.20f, IM_COL32(120, 140, 175, 60), 40, 1.0f);
                        dl->AddCircleFilled(c, r * 0.12f, IM_COL32(9, 10, 15, 255), 32);
                        dl->AddCircle(c, r * 0.12f, IM_COL32(150, 170, 200, 90), 32, 1.0f);
                    }
                }

                /* ---- the footer: what is in the drive, and Launch ---- */
                /*
                 * The title on the left, the button in the bottom right.
                 *
                 * Bottom right is where the thing that commits belongs -- it
                 * is the last place the eye lands on a page read left to
                 * right, and it is where a thumb rests on a handheld held in
                 * two hands. NO DISC over "The tray is empty. Choose one from
                 * Discs." is gone with it: three lines saying what the blank
                 * disc above them already said.
                 */
                ImGui::SetCursorPos(ImVec2(start.x,
                                           start.y + avail.y - btn_h));

                if (!loaded_title.empty() || !message.empty()) {
                    const float lines = (loaded_title.empty() ? 0.0f : 1.0f)
                                      + (message.empty() ? 0.0f : 1.0f);
                    const float block = ImGui::GetTextLineHeight() * lines
                                      + ImGui::GetStyle().ItemSpacing.y
                                            * std::max(0.0f, lines - 1.0f);
                    ImGui::SetCursorPosY(start.y + avail.y - btn_h
                                         + std::max(0.0f, (btn_h - block) * 0.5f));
                    ImGui::BeginGroup();
                    if (!loaded_title.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.42f, 0.71f, 0.97f, 1.0f));
                        ImGui::TextUnformatted(loaded_title.c_str());
                        ImGui::PopStyleColor();
                    }
                    if (!message.empty()) TextDim("%s", message.c_str());
                    ImGui::EndGroup();

                    /* The disc selector, only for games that came on more than
                     * one. A single-disc game showing "Disc 1 of 1" is noise.
                     * Beside the title, because which disc is part of which
                     * game and nothing to do with starting it. */
                    if (loaded_game_index >= 0 && games[loaded_game_index].multi()) {
                        const saturn::Game &g = games[loaded_game_index];
                        ImGui::SameLine();
                        ImGui::SetCursorPosY(start.y + avail.y - btn_h
                                             + (btn_h - ImGui::GetFrameHeight()) * 0.5f);
                        for (size_t i = 0; i < g.discs.size(); ++i) {
                            if (i) ImGui::SameLine();
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

                ImGui::SetCursorPos(ImVec2(start.x + avail.x - btn_w,
                                           start.y + avail.y - btn_h));
                ImGui::BeginDisabled(loaded_game_index < 0);
                /* "Launch", not "Power on". This is the Launch page and the
                 * rail entry that reaches it says Launch; the button that
                 * does the thing should say the same word. */
                if (ImGui::Button("Launch", ImVec2(btn_w, btn_h))) {
                    running_view = true;
                    show_pause = false;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            }   /* face == Face::Launch */
            else {

            ImGui::Spacing();

            /* On a phone there is no room for a rail beside the content, so
             * the same buttons go across in a row. Deliberately the same
             * buttons and the same order -- a layout that rearranges itself is
             * still meant to be the one you learned. */
            if (narrow) {
                /* Four to a row rather than ten: a tenth of a phone screen
                 * is narrower than the word "Processor". They wrap. */
                const int per_row = 4;
                const float bw = (ImGui::GetContentRegionAvail().x -
                                  ImGui::GetStyle().ItemSpacing.x * (per_row - 1))
                               / (float)per_row;
                (void)n_tabs;
                const ImVec2 bsz(bw, fs * 2.0f);
                /*
                 * A row cannot hold ten, so on a narrow screen they wrap.
                 * Same buttons and the same order -- a layout that rearranges
                 * itself is still meant to be the one you learned.
                 */
                const float right = ImGui::GetCursorPosX() +
                                    ImGui::GetContentRegionAvail().x;
                auto flow = [&](const char *label, Face f) {
                    const float step = bw + ImGui::GetStyle().ItemSpacing.x;
                    if (ImGui::GetCursorPosX() + step < right) ImGui::SameLine();
                    tab(label, f, bsz);
                };
                tab("Launch", Face::Launch, bsz);
                flow("Discs", Face::Discs);
                flow("Saves", Face::Saves);
                flow("Picture", Face::Picture);
                flow("Touch pad", Face::Input);
                flow("Sound", Face::Sound);
                flow("Processor", Face::Processor);
                flow("Disc drive", Face::Drive);
                flow("About", Face::About);
                flow("Paths", Face::Paths);
                flow("Setup", Face::Setup);
                ImGui::Spacing();
            }

            /* ============ the face ============ */
            ImGui::BeginChild("##face", ImVec2(0, 0), ImGuiChildFlags_Borders);

            /*
             * Widths inside here are the face's own, not the window's.
             *
             * `cw` is the whole shell, and it was still being used for column
             * positions after the rail took a quarter of it -- so every
             * right-hand column was measured off the edge of the screen and
             * the filenames were cut in half.
             */
            const float fw = ImGui::GetContentRegionAvail().x;

            if (face == Face::Discs) {
                /*
                 * Search is a button, not a field.
                 *
                 * An always-present text box took a fifth of the row for
                 * something used occasionally, and on a handheld tapping it
                 * threw the on-screen keyboard over half the shelf. A modal
                 * asks for the word, and the row keeps its space for the
                 * shelf itself.
                 */
                {
                    char label[96];
                    const char *term = search;
                    if (term[0]) snprintf(label, sizeof label, "Search: %s  X", term);
                    else         snprintf(label, sizeof label, "Search...");
                    if (ImGui::Button(label, ImVec2(0, fs * 1.9f))) {
                        if (term[0]) {
                            /* A second press on a live search clears it: the
                             * way out of a filter should be where the filter
                             * is. */
                            search[0] = 0;
                        } else {
                            ImGui::OpenPopup("Find a game");
                        }
                    }

                    if (ImGui::BeginPopupModal("Find a game", nullptr,
                                               ImGuiWindowFlags_AlwaysAutoResize)) {
                        static char box[96];
                        ImGui::SetNextItemWidth(em * 18.0f);
                        const bool go = ImGui::InputTextWithHint(
                            "##term", "part of a name", box, sizeof box,
                            ImGuiInputTextFlags_EnterReturnsTrue);
                        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
                        ImGui::Spacing();
                        if (go || ImGui::Button("Search", ImVec2(em * 8.0f, 0))) {
                            SDL_strlcpy(search, box, sizeof search);
                            box[0] = 0;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(em * 8.0f, 0))) {
                            box[0] = 0;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::Spacing();
            }

            if (face == Face::Discs) {
                /*
                 * The shelf, and almost nothing else.
                 *
                 * Rescan, the cover/list switch, which artwork to show and the
                 * folder the discs came from all used to sit above the games:
                 * four controls and a path, taking the top third of a handheld
                 * screen, none of them touched more than once a week. They are
                 * on Paths now, next to the folder they are about. What is
                 * left here is the tabs, a row of initials and the discs.
                 */

                /* Which initials there is anything under, in order. */
                {
                    std::string have;
                    for (const saturn::Game &g : games) {
                        const char c = title_initial(g.title);
                        if (have.find(c) == std::string::npos) have.push_back(c);
                    }
                    std::sort(have.begin(), have.end());
                    /* Shown as soon as there is more than one initial to
                     * choose between. A shelf of five is still a shelf you
                     * might want to jump around. */
                    if (have.size() > 1) {
                        ImGui::Spacing();
                        letter_strip(have, disc_letter, fw);
                        ImGui::Spacing();
                        ImGui::Separator();
                    }
                }

                if (games.empty()) {
                    ImGui::Spacing();
                    TextDimWrapped("No disc images here yet. Put .cue, .chd, .iso or "
                                   ".ccd files in the discs folder -- one game per "
                                   "disc, and a game that came on several discs will "
                                   "be grouped back together by its name. Paths is "
                                   "where that folder is set.");
                } else if (shelf_grid) {
                    ImGui::BeginChild("##shelf");
                    /*
                     * Sized to fit a sensible number across, not to a fixed
                     * multiple of the text.
                     *
                     * em * 9 is a reasonable card on a desktop and enormous on
                     * a handheld, where the text is twice the size: three
                     * covers filled a 1080p screen. Aiming for six across and
                     * clamping keeps them recognisable on both.
                     */
                    const float wide_area = ImGui::GetContentRegionAvail().x;
                    const float card_w = std::clamp(wide_area / 6.0f,
                                                    em * 5.0f, em * 9.0f);
                    const float step = card_w + ImGui::GetStyle().ItemSpacing.x;
                    const int per_row = std::max(1, (int)(wide_area / step));
                    int shown = 0;
                    for (size_t i = 0; i < games.size(); ++i) {
                        const saturn::Game &g = games[i];
                        if (search[0] && !SDL_strcasestr(g.title.c_str(), search))
                            continue;
                        if (disc_letter && title_initial(g.title) != disc_letter)
                            continue;
                        if (shown % per_row != 0) ImGui::SameLine();
                        shown++;
                        ImGui::PushID((int)i);

                        char sub[48];
                        if (g.multi()) snprintf(sub, sizeof sub, "%zu discs", g.discs.size());
                        else           snprintf(sub, sizeof sub, "%s",
                                                (int)i == loaded_game_index ? "in the drive" : "");
                        if (cover_card(g, sub, card_w)) {
                            insert_disc((int)i, 0);
                            /* Straight to the machine: choosing a disc and
                             * then having to find the way to the button that
                             * starts it is a step nobody wants. */
                            if (loaded_game_index >= 0) face = Face::Launch;
                        }
                        ImGui::PopID();
                    }
                    scroll_library();
                    ImGui::EndChild();
                } else {
                    ImGui::BeginChild("##list");
                    for (size_t i = 0; i < games.size(); ++i) {
                        const saturn::Game &g = games[i];
                        if (search[0] && !SDL_strcasestr(g.title.c_str(), search))
                            continue;
                        if (disc_letter && title_initial(g.title) != disc_letter)
                            continue;
                        ImGui::PushID((int)i);
                        const bool in_drive = (int)i == loaded_game_index;
                        const ImVec2 at = ImGui::GetCursorScreenPos();
                        const float row_h = fs * 1.9f;
                        const bool pressed = ImGui::InvisibleButton("##game", ImVec2(fw, row_h));
                        library_drag_item |= ImGui::IsItemActive();
                        ImDrawList *dl = ImGui::GetWindowDrawList();
                        if (ImGui::IsItemHovered() || in_drive)
                            dl->AddRectFilled(at, ImVec2(at.x + fw, at.y + row_h),
                                ImGui::GetColorU32(in_drive ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered));
                        dl->PushClipRect(at, ImVec2(at.x + fw * 0.65f, at.y + row_h), true);
                        dl->AddText(ImVec2(at.x, at.y + (row_h - fs) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), g.title.c_str());
                        dl->PopClipRect();
                        if (pressed && library_tap()) {
                            insert_disc((int)i, 0);
                            if (loaded_game_index >= 0) face = Face::Launch;
                        }
                        char detail[64];
                        if (g.multi()) snprintf(detail, sizeof detail, "%zu discs", g.discs.size());
                        else SDL_strlcpy(detail, g.discs[0].file.c_str(), sizeof detail);
                        dl->PushClipRect(ImVec2(at.x + fw * 0.66f, at.y), ImVec2(at.x + fw, at.y + row_h), true);
                        dl->AddText(ImVec2(at.x + fw * 0.66f, at.y + (row_h - fs) * 0.5f), IM_COL32(158, 163, 178, 255), detail);
                        dl->PopClipRect();
                        ImGui::PopID();
                    }
                    scroll_library();
                    ImGui::EndChild();
                }
            }

            else if (face == Face::Memory) {
                /*
                 * The console's own memory, which is not a save state.
                 *
                 * Thirty-two kilobytes of battery-backed SRAM inside the
                 * machine, shared by every game that saves anything, exactly
                 * as on the hardware. It was on the save-states page and did
                 * not belong there: states are per game and this is per
                 * console, and putting them together invited the idea that
                 * deleting one game's states would free space here.
                 */
                ImGui::Spacing();
                ImGui::TextUnformatted("The Saturn's internal memory");
                TextDimWrapped("32 KB of battery-backed memory inside the console, "
                               "shared by every game that saves anything -- as on the "
                               "hardware, where filling it up meant deleting somebody "
                               "else's save.");
                ImGui::Spacing();
                {
                    SDL_PathInfo info;
                    if (SDL_GetPathInfo(bram_path.c_str(), &info) &&
                        info.type == SDL_PATHTYPE_FILE) {
                        SDL_DateTime dt{};
                        if (SDL_TimeToDateTime((SDL_Time)info.modify_time, &dt, true))
                            TextDimWrappedF("%llu bytes, last written %04d-%02d-%02d "
                                            "%02d:%02d", (unsigned long long)info.size,
                                            dt.year, dt.month, dt.day, dt.hour, dt.minute);
                        else
                            TextDimWrappedF("%llu bytes", (unsigned long long)info.size);
                    } else {
                        TextDim("Nothing written yet.");
                    }
                    TextDim("%s", bram_path.c_str());

                    ImGui::Spacing();
                    if (ImGui::Button("Keep a dated copy")) {
                        SDL_DateTime dt{};
                        SDL_Time now = 0;
                        SDL_GetCurrentTime(&now);
                        SDL_TimeToDateTime(now, &dt, true);
                        char stamp[96];
                        snprintf(stamp, sizeof stamp,
                                 "%s/backup-ram-%04d%02d%02d-%02d%02d%02d.bin",
                                 saves_dir.c_str(), dt.year, dt.month, dt.day,
                                 dt.hour, dt.minute, dt.second);
                        say("Copy kept", ymir_bridge_save_internal_backup_memory(ymir, stamp));
                    }
                    ImGui::SameLine();
                    TextDim("before trying something that might overwrite it");

                    /* The copies, so one can be found again. */
                    int n = 0;
                    char **found = SDL_GlobDirectory(saves_dir.c_str(), "backup-ram-*.bin",
                                                     SDL_GLOB_CASEINSENSITIVE, &n);
                    if (n > 0) {
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextUnformatted("Copies you have kept");
                        ImGui::BeginChild("##brambk");
                        for (int i = 0; found && i < n; ++i) {
                            ImGui::PushID(i);
                            ImGui::TextUnformatted(found[i]);
                            ImGui::SameLine(fw * 0.62f);
                            if (ImGui::SmallButton("Restore")) {
                                const std::string src = saves_dir + "/" + found[i];
                                say("Restored", ymir_bridge_load_internal_backup_memory(
                                                    ymir, src.c_str(), 0));
                                /* Back onto the live file, or the restore is
                                 * lost the moment the machine writes again. */
                                ymir_bridge_save_internal_backup_memory(ymir, bram_path.c_str());
                                load_bram();
                            }
                            ImGui::SameLine(fw * 0.82f);
                            ImGui::SmallButton("Delete");
                            if (ImGui::IsItemActive() &&
                                ImGui::GetIO().MouseDownDuration[0] > 0.6f) {
                                const std::string f = saves_dir + "/" + found[i];
                                SDL_RemovePath(f.c_str());
                                say("Deleted", YMIR_OK);
                            }
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold to delete");
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                    if (found) SDL_free(found);
                }
                if (!state_message.empty() &&
                    SDL_GetTicks() - state_message_at <= 5000)
                    ImGui::TextUnformatted(state_message.c_str());
            }

            else if (face == Face::Saves) {
                /*
                 * Save states, by game, laid out like the shelf.
                 *
                 * A state belongs to one disc and to one slot, so a flat list
                 * of filenames was the wrong shape: the question people
                 * actually have is "what have I got saved for THIS game", and
                 * that is a heading with four slots under it.
                 */
                ImGui::Spacing();
                TextDimWrapped("A snapshot of the whole machine, taken whenever you "
                               "press Save and never overwritten. Separate from the "
                               "console's memory, which every game shares -- that is "
                               "on the Memory page.");
                ImGui::Spacing();
                TextDim("%s", states_dir.c_str());
                ImGui::Separator();

                /*
                 * Every save in the folder, filed under the game it belongs
                 * to and newest first.
                 *
                 * A save is <stem>.<stamp>.state; the old four-slot files are
                 * <stem>.sN and are listed too, because somebody who saved
                 * before this changed should not find their saves missing.
                 * The game is whatever comes before the first dot after the
                 * stem, which is why the stamp has no dots in it.
                 */
                struct Save { std::string file, label; Uint64 when = 0, bytes = 0; };
                std::map<std::string, std::vector<Save>> by_game;
                {
                    int n = 0;
                    char **found = SDL_GlobDirectory(states_dir.c_str(), "*", 0, &n);
                    for (int i = 0; found && i < n; ++i) {
                        const std::string name = found[i];
                        std::string game, label;
                        if (name.size() > 6 &&
                            name.compare(name.size() - 6, 6, ".state") == 0) {
                            const size_t stamp = name.rfind('.', name.size() - 7);
                            if (stamp == std::string::npos) continue;
                            game  = name.substr(0, stamp);
                            label = name.substr(stamp + 1, name.size() - stamp - 7);
                        } else {
                            const size_t dot = name.rfind(".s");
                            if (dot == std::string::npos || dot + 2 >= name.size()) continue;
                            const char slot = name[dot + 2];
                            if (slot < '1' || slot > '4') continue;
                            game  = name.substr(0, dot);
                            label = std::string("slot ") + slot;
                        }
                        SDL_PathInfo info;
                        const std::string full = states_dir + "/" + name;
                        if (!SDL_GetPathInfo(full.c_str(), &info) ||
                            info.type != SDL_PATHTYPE_FILE) continue;
                        Save sv;
                        sv.file  = full;
                        sv.label = label;
                        sv.when  = (Uint64)info.modify_time;
                        sv.bytes = (Uint64)info.size;
                        by_game[game].push_back(sv);
                    }
                    if (found) SDL_free(found);
                    for (auto &kv : by_game)
                        std::sort(kv.second.begin(), kv.second.end(),
                                  [](const Save &a, const Save &b) {
                                      return a.label > b.label;
                                  });
                }

                if (by_game.empty()) {
                    ImGui::Spacing();
                    TextDimWrapped("Nothing saved yet. In a game, press Escape and "
                                   "press Save, or F5 without opening anything. Every "
                                   "save is kept -- they are listed here, and this is "
                                   "where they are deleted.");
                } else {
                    ImGui::BeginChild("##states");
                    for (auto &kv : by_game) {
                        ImGui::PushID(kv.first.c_str());
                        ImGui::TextUnformatted(kv.first.c_str());
                        for (size_t i = 0; i < kv.second.size(); ++i) {
                            const Save &sv = kv.second[i];
                            ImGui::PushID((int)i);
                            SDL_DateTime dt{};
                            if (SDL_TimeToDateTime((SDL_Time)sv.when, &dt, true))
                                ImGui::Text("   %04d-%02d-%02d %02d:%02d",
                                            dt.year, dt.month, dt.day, dt.hour, dt.minute);
                            else
                                ImGui::Text("   %s", sv.label.c_str());
                            ImGui::SameLine(fw * 0.30f);
                            TextDim("%.1f MB", (double)sv.bytes / (1024.0 * 1024.0));
                            ImGui::SameLine(fw * 0.78f);
                            ImGui::SmallButton("Delete");
                            /* Held, not tapped. There is no undo for this and
                             * the list is full of things worth keeping. */
                            if (ImGui::IsItemActive() &&
                                ImGui::GetIO().MouseDownDuration[0] > 0.6f) {
                                SDL_RemovePath(sv.file.c_str());
                                sync_saves();
                                say("Deleted", YMIR_OK);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Hold to delete");
                            ImGui::PopID();
                        }
                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (!state_message.empty() &&
                    SDL_GetTicks() - state_message_at <= 5000)
                    ImGui::TextUnformatted(state_message.c_str());
            }

            else if (face == Face::Saves) {
                /*
                 * Save states, by game, laid out like the shelf.
                 *
                 * A state belongs to one disc and to one slot, so a flat list
                 * of filenames was the wrong shape: the question people
                 * actually have is "what have I got saved for THIS game", and
                 * that is a heading with four slots under it.
                 */
                ImGui::Spacing();
                TextDimWrapped("A snapshot of the whole machine, taken whenever you "
                               "press Save and never overwritten. Separate from the "
                               "console's memory, which every game shares -- that is "
                               "on the Memory page.");
                ImGui::Spacing();
                TextDim("%s", states_dir.c_str());
                ImGui::Separator();

                /* Gather <stem>.sN into one entry per game. */
                struct Slot { bool present = false; Uint64 when = 0; Uint64 bytes = 0; };
                std::map<std::string, std::array<Slot, 4>> by_game;
                {
                    int n = 0;
                    char **found = SDL_GlobDirectory(states_dir.c_str(), "*", 0, &n);
                    for (int i = 0; found && i < n; ++i) {
                        const std::string name = found[i];
                        const size_t dot = name.rfind(".s");
                        if (dot == std::string::npos || dot + 2 >= name.size()) continue;
                        const int slot = name[dot + 2] - '1';
                        if (slot < 0 || slot > 3) continue;
                        SDL_PathInfo info;
                        const std::string full = states_dir + "/" + name;
                        if (!SDL_GetPathInfo(full.c_str(), &info) ||
                            info.type != SDL_PATHTYPE_FILE) continue;
                        Slot &sl = by_game[name.substr(0, dot)][(size_t)slot];
                        sl.present = true;
                        sl.when = (Uint64)info.modify_time;
                        sl.bytes = (Uint64)info.size;
                    }
                    if (found) SDL_free(found);
                }

                if (by_game.empty()) {
                    ImGui::Spacing();
                    TextDimWrapped("Nothing saved yet. In a game, press Escape and use "
                                   "the slots there, or F5 to save and F8 to load.");
                } else {
                    ImGui::BeginChild("##states");
                    for (auto &kv : by_game) {
                        ImGui::PushID(kv.first.c_str());
                        ImGui::TextUnformatted(kv.first.c_str());
                        for (int i = 0; i < 4; ++i) {
                            const Slot &sl = kv.second[(size_t)i];
                            ImGui::PushID(i);
                            ImGui::Text("   Slot %d", i + 1);
                            ImGui::SameLine(fw * 0.16f);
                            if (!sl.present) {
                                TextDim("empty");
                            } else {
                                SDL_DateTime dt{};
                                if (SDL_TimeToDateTime((SDL_Time)sl.when, &dt, true))
                                    TextDim("%04d-%02d-%02d %02d:%02d   %.1f MB",
                                            dt.year, dt.month, dt.day, dt.hour, dt.minute,
                                            (double)sl.bytes / (1024.0 * 1024.0));
                                else
                                    TextDim("%.1f MB", (double)sl.bytes / (1024.0 * 1024.0));
                                ImGui::SameLine(fw * 0.78f);
                                ImGui::SmallButton("Delete");
                                if (ImGui::IsItemActive() &&
                                    ImGui::GetIO().MouseDownDuration[0] > 0.6f) {
                                    const std::string f = states_dir + "/" + kv.first +
                                                          ".s" + std::to_string(i + 1);
                                    SDL_RemovePath(f.c_str());
                                    sync_saves();
                                    say("Deleted", YMIR_OK);
                                }
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("Hold to delete");
                            }
                            ImGui::PopID();
                        }
                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (!state_message.empty() &&
                    SDL_GetTicks() - state_message_at <= 5000)
                    ImGui::TextUnformatted(state_message.c_str());
            }

            else if (face == Face::Setup || face == Face::Paths ||
                     face == Face::Picture || face == Face::Sound ||
                     face == Face::Processor || face == Face::Drive ||
                     face == Face::Input) {
                /*
                 * The settings pages, one per rail entry.
                 *
                 * They share a block because they share the two-column helper
                 * and the one `dirty` flag that decides whether the machine
                 * needs telling; which page is drawn is a plain test on the
                 * face. Named after the parts of the console rather than after
                 * this application, so a setting is where somebody would go
                 * looking for it.
                 */
                saturn::Settings &s = cfg.machine;
                bool dirty = false;

                /*
                 * Two columns where there is width for two, one where there
                 * is not.
                 *
                 * A settings page you have to scroll is a settings page where
                 * half the answers are out of sight, and the pages here are
                 * mostly short blocks of related switches -- exactly the shape
                 * that pairs up well. On a phone the same blocks simply run
                 * down the page with a rule between them.
                 */
                auto col_begin = [&](const char *id) {
                    if (two_col)
                        ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame);
                    if (two_col) { ImGui::TableNextRow(); ImGui::TableNextColumn(); }
                };
                auto col_next = [&] {
                    if (two_col) ImGui::TableNextColumn();
                    else { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }
                };
                auto col_end = [&] { if (two_col) ImGui::EndTable(); };

                {
                    if (face == Face::Paths) {
                        static char parent_buf[1024] = {};
                        static char bios_buf[1024] = {};
                        static char saves_buf[1024] = {};
                        static bool primed = false;
                        if (!primed) {
                            SDL_strlcpy(parent_buf, cfg.parent_root.c_str(), sizeof parent_buf);
                            SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                            SDL_strlcpy(saves_buf, cfg.saves_dir.c_str(), sizeof saves_buf);
                            primed = true;
                        }
                        auto use_parent = [&](const std::string &root) {
                            save_bram();
                            saturn::configure_parent_folder(cfg, root);
                            resolve_saves();
                            load_bram();
                            rescan();
                            adopt_bios_from_discs();
                            SDL_strlcpy(parent_buf, cfg.parent_root.c_str(), sizeof parent_buf);
                            SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                            SDL_strlcpy(saves_buf, cfg.saves_dir.c_str(), sizeof saves_buf);
                            saturn::save_app_config(cfg_path, cfg);
                        };
                        col_begin("##pathcols");
                        ImGui::TextUnformatted("Parent folder");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##parent", parent_buf, sizeof parent_buf,
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                            use_parent(parent_buf);
                        TextDimWrapped("Contains bios, games, artwork and saves. Existing folder names and flat disc libraries are supported.");
                        if (saturn::pickers_usable()) {
                            ImGui::BeginDisabled(saturn::pick_in_progress());
                            if (ImGui::Button("Choose parent folder...")) saturn::begin_pick_folder(cfg.parent_root);
                            ImGui::EndDisabled();
                        } else if (saturn::saf_available()) {
                            if (ImGui::Button("Choose parent folder...")) saturn::saf_pick();
                            for (const saturn::SafTree &tree : saturn::saf_trees()) {
                                ImGui::PushID(tree.uri.c_str());
                                if (tree.uri == cfg.disc_tree || (!tree.path.empty() && tree.path == cfg.parent_root))
                                    TextDimWrappedF("Using %s", tree.name.c_str());
                                else if (ImGui::Button(tree.name.c_str())) {
                                    save_bram();
                                    adopt_tree(tree.uri);
                                    resolve_saves();
                                    load_bram();
                                    rescan();
                                    adopt_bios_from_discs();
                                    SDL_strlcpy(parent_buf, cfg.parent_root.c_str(), sizeof parent_buf);
                                    SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                                    SDL_strlcpy(saves_buf, cfg.saves_dir.c_str(), sizeof saves_buf);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::Spacing();
                        ImGui::TextUnformatted("BIOS");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##bios", bios_buf, sizeof bios_buf,
                                             ImGuiInputTextFlags_EnterReturnsTrue)) {
                            cfg.bios_path = bios_buf;
                            load_bios();
                            saturn::save_app_config(cfg_path, cfg);
                        }
                        if (saturn::pickers_usable()) {
                            ImGui::BeginDisabled(saturn::pick_in_progress());
                            if (ImGui::Button("Browse BIOS...")) saturn::begin_pick_file(cfg.parent_root.empty() ? cfg.disc_root : saturn::folder_for(cfg.parent_root, "bios"));
                            ImGui::EndDisabled();
                        } else if (ImGui::Button("Find the BIOS")) {
                            adopt_bios_from_discs();
                            SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                        }
                        ImGui::SameLine();
                        TextDim(bios_loaded ? "Loaded" : "Not loaded");
                        std::string picked;
                        if (saturn::take_pick(picked)) {
                            SDL_PathInfo info;
                            if (SDL_GetPathInfo(picked.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
                                use_parent(picked);
                            else {
                                cfg.bios_path = picked;
                                SDL_strlcpy(bios_buf, picked.c_str(), sizeof bios_buf);
                                load_bios();
                                saturn::save_app_config(cfg_path, cfg);
                            }
                        }

                        col_next();
                        ImGui::TextUnformatted("Artwork");
                        TextDimWrappedF("%s", cfg.disc_tree.empty() ? cfg.artwork_dir.c_str()
                            : saturn::saf_folder_name(cfg.disc_tree, "artwork").c_str());
                        TextDimWrapped("Place PNG or JPEG images here with the same name as the disc. For example: Daytona USA.chd uses Daytona USA.png. A multi-disc game can use its shared title.");
                        if (ImGui::Button("Refresh artwork and library")) rescan();
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Discs");
                        TextDimWrappedF("%zu games in %s", games.size(), cfg.disc_tree.empty()
                            ? cfg.disc_root.c_str() : saturn::saf_folder_name(cfg.disc_tree, "cd").c_str());
                        if (ImGui::Button(shelf_grid ? "Show as a list" : "Show the covers")) shelf_grid = !shelf_grid;
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Saves");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::BeginDisabled(!cfg.disc_tree.empty());
                        if (ImGui::InputText("##saves", saves_buf, sizeof saves_buf,
                                             ImGuiInputTextFlags_EnterReturnsTrue)) {
                            save_bram();
                            cfg.saves_dir = saves_buf;
                            resolve_saves();
                            load_bram();
                            saturn::save_app_config(cfg_path, cfg);
                        }
                        ImGui::EndDisabled();
                        TextDimWrappedF("%s", cfg.disc_tree.empty() ? saves_dir.c_str()
                            : saturn::saf_folder_name(cfg.disc_tree, "saves").c_str());
                        if (saves_sync_failed)
                            TextDimWrapped("Could not update the selected saves folder. Progress is retained on this device; restore folder access to sync it.");
                        col_end();
                    }

                    if (face == Face::Setup) {
                        ImGui::Spacing();
                        col_begin("##setupcols");
                        
                        if (ImGui::Button("Run setup again")) { wizard = true; wstep = 0; }
                        col_end();
                    }


                    if (face == Face::Input) {
                        /*
                         * The on-screen pad.
                         *
                         * Two tabs, restructured 2026-09-18 (mirrors the
                         * change in Retro-PSX-SDL3):
                         *   Edit   - the meta: which pad, when it shows.
                         *            Rail is visible. Nothing else.
                         *   Review - the pad itself, drawn full-screen
                         *            (rail collapses so the 16:9 preview
                         *            takes the whole window). NO chrome.
                         *            Tapping a cluster pops a modal;
                         *            tapping the cluster's "Done" or
                         *            pressing Escape cascades -- closes
                         *            the modal AND exits the preview,
                         *            returning to the Edit tab. The user
                         *            is never stranded on a chrome-free
                         *            preview with no visible way out.
                         */
                        pad_preview_size = ImVec2(0, 0);
                        if (g_pad_tab == 0) {
                            g_pad_fullscreen = 0;
                            if (ImGui::BeginTabBar("##pad_tabs")) {
                                if (ImGui::BeginTabItem("Edit")) {
                                    ImGui::Spacing();
                                    ImGui::TextUnformatted("Pad");
                                    ImGui::SameLine();
                                    {
                                        const touchpad::Profile *choices[] = {
                                            &touchpad::profile_xbox360(), &touchpad::profile_generic(),
                                            &touchpad::profile_saturn() };
                                        for (const touchpad::Profile *pp : choices) {
                                            if (ImGui::RadioButton(pp->name.c_str(),
                                                                   cfg.machine.touch_pad == pp->id) &&
                                                cfg.machine.touch_pad != pp->id) {
                                                cfg.machine.touch_pad = pp->id;
                                                dirty = true;
                                                load_pad();
                                            }
                                            ImGui::SameLine();
                                        }
                                        ImGui::NewLine();
                                    }
                                    ImGui::TextUnformatted("Show it");
                                    ImGui::SameLine();
                                    dirty |= ImGui::RadioButton("On a touchscreen", &s.touch_pad_show, 0);
                                    ImGui::SameLine();
                                    dirty |= ImGui::RadioButton("Always", &s.touch_pad_show, 1);
                                    ImGui::SameLine();
                                    dirty |= ImGui::RadioButton("Never", &s.touch_pad_show, 2);
                                    ImGui::Spacing();
                                    if (ImGui::Button("Arrange pad (full screen)")) {
                                        g_pad_tab = 1;
                                        g_pad_fullscreen = 1;
                                    }
                                    ImGui::EndTabItem();
                                }
                                ImGui::EndTabBar();
                            }
                        }
                        if (g_pad_tab == 1) {
                            g_pad_fullscreen = 1;
                            /* Full-window preview with no chrome.
                             * Modal opens on tap; Done/Escape both
                             * cascade to also exit the preview (see
                             * the check after the EndPopup below). */
                            const ImVec2 win = ImGui::GetWindowSize();
                            const float avail_w = win.x;
                            const float avail_h = win.y;
                            float bw_ = avail_w;
                            float bh_ = bw_ * 9.0f / 16.0f;
                            if (bh_ > avail_h) { bh_ = avail_h; bw_ = bh_ * 16.0f / 9.0f; }
                            const float ox = (avail_w - bw_) * 0.5f;
                            const float oy = (avail_h - bh_) * 0.5f;
                            const ImVec2 pos(ox, oy);
                            ImDrawList *dl = ImGui::GetWindowDrawList();
                            dl->AddRectFilled(pos, ImVec2(pos.x + bw_, pos.y + bh_),
                                              IM_COL32(16, 16, 20, 255), 8.0f);
                            dl->AddRect(pos, ImVec2(pos.x + bw_, pos.y + bh_),
                                        IM_COL32(255, 255, 255, 60), 8.0f);
                            const ImVec2 pad_pos(ox, oy);
                            const ImVec2 pad_size(bw_, bh_);
                            ImGui::SetCursorScreenPos(pad_pos);
                            ImGui::InvisibleButton("##padpreview", pad_size);
                            pad_preview_pos = pad_pos;
                            pad_preview_size = pad_size;
                            pad.draw(dl, pad_pos, pad_preview_size);
                            /* Modal: general panel + cluster panel
                             * together. Cluster tap opens it. */
                            if (pad.has_selection() &&
                                !ImGui::IsPopupOpen("##pad_designer")) {
                                ImGui::OpenPopup("##pad_designer");
                            }
                            if (ImGui::BeginPopupModal("##pad_designer", nullptr,
                                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                                if (pad.designer_controls(nullptr)) pad.layout().save(cfg_dir);
                                if (pad.has_selection()) {
                                    ImGui::Separator();
                                    if (pad.cluster_panel()) pad.layout().save(cfg_dir);
                                } else {
                                    ImGui::Spacing();
                                    TextDim("Tap a cluster in the preview to resize or hide it.");
                                }
                                ImGui::EndPopup();
                            }
                            /* Cascade exit: when the modal closes and
                             * nothing is selected any more, drop the
                             * user back on the Edit tab too, not
                             * stranded on a chrome-free preview.
                             *
                             * Important: the gate uses the modal's
                             * just-closed state, not "no popup right
                             * now" -- tapping 'Arrange pad' starts
                             * the preview with no selection and no
                             * popup, which would otherwise pass the
                             * naive check and flash the preview off
                             * on the same frame the user opened it. */
                            {
                                static bool was_modal_open = false;
                                const bool modal_open =
                                    ImGui::IsPopupOpen("##pad_designer");
                                if (g_pad_tab == 1 && !pad.has_selection() &&
                                    was_modal_open && !modal_open) {
                                    g_pad_tab = 0;
                                    g_pad_fullscreen = 0;
                                }
                                was_modal_open = modal_open;
                            }
                        }
                    }

                    if (face == Face::Processor) {
                        ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Work out the region from the disc",
                                                 &s.region_auto);
                        if (!s.region_auto) {
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("NTSC", &s.video_standard, 0);
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("PAL", &s.video_standard, 1);
                        }
                        ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Emulate the SH2 cache (accurate, slower)",
                                                 &s.sh2_cache);
                        ImGui::SetNextItemWidth(fw * (two_col ? 0.40f : 0.45f));
                        dirty |= ImGui::SliderInt("Processor speed", &s.sh2_clock,
                                                  50, 300, "%d%%");
                        TextDimWrapped("100% is the real machine. Games written for it "
                                       "can and do break when it is faster.");
                    }

                    if (face == Face::Picture) {
                        /*
                         * Switches, not an essay.
                         *
                         * Every control here had a paragraph under it saying
                         * why it exists, which meant the page scrolled and the
                         * thing you came to change was below the fold. The
                         * reasoning lives in the comments and in the commit
                         * that added each one; the screen just needs the
                         * choice. A hover tooltip carries the one sentence
                         * worth keeping.
                         */
                        auto why = [&](const char *text) {
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
                        };

                        ImGui::Spacing();
                        col_begin("##picturecols");

                        ImGui::TextUnformatted("Smoothing");
                        {
                            int sc = s.scaling;
                            dirty |= ImGui::RadioButton("Automatic", &sc, 0);
                            why("Hard pixels when the window is a whole multiple of "
                                "the picture, smooth when it is not.");
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Sharp", &sc, 1);
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Smooth", &sc, 2);
                            s.scaling = sc;
                        }

                        ImGui::Spacing();
                        ImGui::TextUnformatted("Shape");
                        {
                            int as_ = s.aspect;
                            dirty |= ImGui::RadioButton("4:3", &as_, 0);
                            why("The shape a Saturn was drawn for.");
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Fill the window", &as_, 1);
                            s.aspect = as_;
                        }
                        ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Whole-number scaling", &s.integer_scale);
                        why("Every pixel the same size, with a border. Overrides the "
                            "shape: 320x224 is not 4:3.");

                        col_next();

                        ImGui::TextUnformatted("Light gun crosshair");
                        ImGui::SetNextItemWidth(fw * (two_col ? 0.40f : 0.55f));
                        dirty |= ImGui::SliderInt("##crosshair", &s.crosshair, 50, 600, "%d%%");
                        why("Only drawn when a port is holding a Virtua Gun.");

                        ImGui::Spacing();
                        ImGui::TextUnformatted("Rendering");
                        dirty |= ImGui::Checkbox("Threaded VDP1", &s.threaded_vdp1);
                        dirty |= ImGui::Checkbox("Threaded VDP2", &s.threaded_vdp2);
                        dirty |= ImGui::Checkbox("Threaded deinterlacer", &s.threaded_deinterlace);
                        why("The video processors are most of this emulator's work. "
                            "Turn one off only to find out whether it is the cause of "
                            "something.");
                        col_end();
                    }

                    if (face == Face::Sound) {
                        ImGui::Spacing();
                        int interp = s.audio_interpolation;
                        dirty |= ImGui::RadioButton("Linear (as the hardware)", &interp, 1);
                        ImGui::SameLine();
                        dirty |= ImGui::RadioButton("Nearest (harsher)", &interp, 0);
                        s.audio_interpolation = interp;
                        TextDimWrapped("The SCSP interpolates linearly, which makes the "
                                       "accurate choice the one that sounds like an "
                                       "improvement.");

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Mute", &s.audio_muted);

                        /*
                         * A meter, because "there is no sound" is otherwise
                         * unanswerable from inside the app. If this moves, the
                         * Saturn is making sound and the fault is past us --
                         * the output device, its volume, or the mixer. If it
                         * sits at nothing while a game plays, the fault is
                         * ours.
                         */
                        const int lvl = std::clamp(ymir_bridge_get_audio_level(ymir), 0, 100);
                        char meter[32];
                        if (lvl > 0) std::snprintf(meter, sizeof meter, "%d%%", lvl);
                        else         std::snprintf(meter, sizeof meter, "silent");
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Output");
                        ImGui::ProgressBar(lvl / 100.0f, ImVec2(fw * 0.4f, 0.0f), meter);
                        ImGui::SameLine();
                        ImGui::Text("%d ms behind", ymir_bridge_get_audio_queue_ms(ymir));
                        TextDimWrapped("The level leaving the emulator, and how far the "
                                       "sound trails the picture. The machine paces "
                                       "itself to hold that around 60 ms; if it climbs "
                                       "and stays climbed, the emulator is running "
                                       "faster than the sound card can take it.");
                    }

                    if (face == Face::Drive) {
                        ImGui::Spacing();
                        ImGui::SetNextItemWidth(fw * (two_col ? 0.40f : 0.45f));
                        dirty |= ImGui::SliderInt("Read speed", &s.cd_read_speed,
                                                  2, 200, "%dx");
                        TextDimWrapped("2x is the real drive. Faster cuts loading, and a "
                                       "few titles that stream from the disc in time "
                                       "with the music will notice.");
                        ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Low-level CD block (slower, reads more "
                                                 "discs)", &s.cdblock_lle);
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextUnformatted("Clock");
                        TextDimWrapped("The Saturn takes its clock from this device the "
                                       "first time it runs, and remembers it -- which is "
                                       "why the BIOS does not ask. There is no switch "
                                       "for the core's RTC mode: setting it crashes "
                                       "inside Ymir, and a control that ends the app is "
                                       "worse than no control.");
                    }


                }

                if (dirty) {
                    apply_options();
                    saturn::save_app_config(cfg_path, cfg);
                }
            }

            else {
                ImGui::Text("Retro-Saturn %s (%s)", SATURN_APP_VERSION, SATURN_APP_BUILD);
                TextDimWrapped("A Sega Saturn emulator. The emulation is Ymir's work, "
                               "not ours.");

                /*
                 * The controls, described where reference material lives.
                 *
                 * There was a page for this, and all it really held was the
                 * two port selectors -- which are on Launch, under the
                 * sockets they belong to, so the page was a second way to
                 * change a thing that is already on screen. What it had that
                 * Launch does not is the answer to "my pad does nothing",
                 * and that is a question to look up, not a control.
                 */
                ImGui::Spacing();
                ImGui::TextUnformatted("Controls");
                if (pads.empty()) {
                    TextDimWrapped("No gamepad found -- keyboard only.");
                } else {
                    TextDimWrappedF("%zu gamepad%s: %s", pads.size(),
                                    pads.size() == 1 ? "" : "s",
                                    SDL_GetGamepadName(pads[0])
                                        ? SDL_GetGamepadName(pads[0]) : "unnamed");
                }
                TextDimWrapped("On a modern pad the bumpers are the Saturn's L and R, "
                               "and the triggers are the pedals: right accelerates, "
                               "left brakes. C and Z are the stick clicks. The Virtua "
                               "Gun is aimed with the mouse or a finger -- left fires, "
                               "right reloads, middle is Start. Which peripheral is in "
                               "which port is set on Launch, under the console.");

                ImGui::Spacing();
                ImGui::TextUnformatted("Ymir");
                ImGui::Text("version %s", SATURN_CORE_VERSION);
                /* The date the core was last refreshed from upstream, so
                 * "are we behind?" is answerable from inside the app rather
                 * than by reading a submodule pointer. */
                TextDim("%s, refreshed %s", SATURN_CORE_DESC, SATURN_CORE_DATE);
                ImGui::Spacing();
                TextDimWrapped("github.com/ymir-emu/Ymir -- GNU GPL v3 or later. The "
                               "app source and pinned core are at "
                               "github.com/CrownParkComputing/Retro-Saturn.");
                ImGui::Spacing();
                TextDimWrapped("SDL3: zlib. Dear ImGui: MIT.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                TextDimWrapped("No BIOS and no games are included. Both are yours to "
                               "supply, from hardware and discs you own.");
            }
            ImGui::EndChild();

            }   /* everything that is not Launch */
            ImGui::EndChild();     /* ##right */

            ImGui::End();
        }

        /* ---- the emulator, filling the window ---- */
        else {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);

            if (demo_session && !show_pause) {
                ImGui::SetNextWindowPos(ImVec2((float)win_w - 12.0f, win_h * 0.5f),
                                       ImGuiCond_Always, ImVec2(1.0f, 0.5f));
                ImGui::Begin("##demo_return", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings);
                if (ImGui::Button("Return to\nwizard", ImVec2(0, ImGui::GetFontSize() * 3.2f)))
                    return_to_wizard();
                ImGui::End();
            }

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
                /* The touch pad, where it can be used. Which pad and how it
                 * is laid out are on the Touch pad page; this is only show,
                 * hide, and move things while the game is up. */
                if (cfg.machine.port1 != saturn::Peripheral::VirtuaGun &&
                    cfg.machine.port1 != saturn::Peripheral::ShuttleMouse) {
                    ImGui::Separator();
                    const bool on = pad_on();
                    if (ImGui::Button(on ? "Hide the touch pad" : "Show the touch pad", bw)) {
                        cfg.machine.touch_pad_show = on ? 2 : 1;
                        saturn::save_app_config(cfg_path, cfg);
                    }
                    if (on && ImGui::Button("Arrange the touch pad", bw)) {
                        pad_editing = true;
                        show_pause = false;
                    }
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
                /*
                 * Save writes a new one; Load takes the last.
                 *
                 * This used to be four radio buttons, a line saying what was
                 * in the chosen slot, and two peripheral pickers -- enough to
                 * push the panel past the bottom of a handheld screen, so the
                 * way out of the game was below a scrollbar. What is left is
                 * the two things anybody opens this for. Every save is kept;
                 * the Saves page lists them and is where they are deleted.
                 */
                const ImVec2 hw(bw.x * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, 0);
                if (ImGui::Button("Save", hw)) do_save_state();
                ImGui::SameLine();
                if (ImGui::Button("Load last", hw)) {
                    do_load_state();
                    show_pause = false;
                }
                TextDim("F5 saves, F8 loads, without opening this.");
                /* The last word on a save or a load, for a few seconds.
                 * Long enough to read, short enough that it is not still
                 * there claiming something about a state you have since
                 * replaced. */
                if (!state_message.empty()) {
                    if (SDL_GetTicks() - state_message_at > 5000) state_message.clear();
                    else ImGui::TextUnformatted(state_message.c_str());
                }

                ImGui::Separator();
                if (ImGui::Button(demo_session ? "Return to wizard" : "Back to the shelf", bw)) {
                    /* Written here too, not only at exit: an app that is
                     * force-quit should not forget the clock -- or, far
                     * worse, an afternoon of somebody's progress. */
                    if (demo_session) return_to_wizard();
                    else {
                        ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
                        save_bram();
                        pointer.seen = false;
                        running_view = false;
                        show_pause = false;
                    }
                }
                ImGui::End();
            }
        }

        /*
         * A crosshair, because a gun you cannot see is a gun you cannot aim.
         *
         * Drawn by us rather than left to the system cursor: the cursor is
         * hidden over the picture so it does not sit a few pixels away from
         * where the Saturn thinks the shot went, and an arrow is the wrong
         * shape for aiming anyway.
         */
        const bool gun_in_a_port = cfg.machine.port1 == saturn::Peripheral::VirtuaGun ||
                                   cfg.machine.port2 == saturn::Peripheral::VirtuaGun;
        /* Inside the picture, not merely inside the window: the letterbox bars
         * are not part of the Saturn's screen and a sight floating on them is
         * aiming at nothing. */
        const PictureRect gun_r = picture_rect(win_w, win_h, frame_w, frame_h,
                                               cfg.machine);
        const bool pointer_on_picture =
            pointer.x >= gun_r.x && pointer.x < gun_r.x + gun_r.w &&
            pointer.y >= gun_r.y && pointer.y < gun_r.y + gun_r.h;

        if (running_view && !show_pause && pointer.seen && gun_in_a_port &&
            pointer_on_picture) {
            ImDrawList *dl = ImGui::GetForegroundDrawList();
            const ImVec2 c(pointer.x, pointer.y);
            /* Sized off the window, not the font: a gun sight has to be found
             * on a busy screen at a glance, and the first one was drawn at
             * text size and disappeared into the scenery. */
            const float a = (float)win_h * 0.028f *
                            ((float)cfg.machine.crosshair / 100.0f);
            const float t = std::max(2.0f, a * 0.09f);   /* line weight */
            const ImU32 ink = pointer.trigger ? IM_COL32(255, 96, 64, 245)
                                              : IM_COL32(120, 200, 255, 225);
            /* A dark pass underneath, offset by nothing but drawn thicker, so
             * the sight stays visible over a light wall as well as a dark
             * one. Without it a blue crosshair vanishes against blue sky. */
            const ImU32 shadow = IM_COL32(0, 0, 0, 150);
            for (int pass = 0; pass < 2; ++pass) {
                const ImU32 col = pass ? ink : shadow;
                const float wgt = pass ? t : t + 2.0f;
                dl->AddCircle(c, a * 0.55f, col, 32, wgt);
                dl->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x - a * 0.28f, c.y), col, wgt);
                dl->AddLine(ImVec2(c.x + a * 0.28f, c.y), ImVec2(c.x + a, c.y), col, wgt);
                dl->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y - a * 0.28f), col, wgt);
                dl->AddLine(ImVec2(c.x, c.y + a * 0.28f), ImVec2(c.x, c.y + a), col, wgt);
            }
            dl->AddCircleFilled(c, std::max(1.5f, a * 0.06f), ink, 12);
        }

        /* ---- the on-screen pad, over the picture ---- */
        if (pad_active) {
            pad.draw(ImGui::GetForegroundDrawList(), ImVec2(0, 0), win_sz);
            if (pad_editing) {
                /* The designer's controls, in a strip at the top while the
                 * game waits underneath. Done goes back to playing. */
                ImGui::SetNextWindowPos(ImVec2(win_w * 0.5f, 8.0f), ImGuiCond_Always,
                                        ImVec2(0.5f, 0.0f));
                ImGui::Begin("Arrange the pad", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoCollapse);
                TextDim("Drag a control to move it. Tap one to resize or hide it.");
                if (pad.designer_controls(&pad_sink)) pad.layout().save(cfg_dir);
                ImGui::Separator();
                if (ImGui::Button("Done", ImVec2(ImGui::GetFontSize() * 8.0f, 0))) {
                    pad_editing = false;
                    pad.layout().save(cfg_dir);
                }
                ImGui::End();
            }
        }
        if (pad.take_dirty()) pad.layout().save(cfg_dir);

        /* The system cursor gets out of the way for the pointing devices, and
         * comes back for everything else -- including the menu, which you
         * still have to be able to click. */
        {
            /* Hidden only where the app is drawing a sight instead. Every
             * menu, and the shelf, keeps an ordinary pointer -- they are
             * things you click, and a crosshair over a list of games is the
             * app wearing the game's clothes. */
            const bool aiming = running_view && !show_pause && pointer_on_picture &&
                (gun_in_a_port ||
                 cfg.machine.port1 == saturn::Peripheral::ShuttleMouse ||
                 cfg.machine.port2 == saturn::Peripheral::ShuttleMouse);
            static bool hidden = false;
            if (aiming != hidden) {
                hidden = aiming;
                if (aiming) SDL_HideCursor(); else SDL_ShowCursor();
            }
        }

        lifecycle.set_playing(running_view && !show_pause);
        if (!running_view || show_pause) pad.release_all(pad_sink);

        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 14, 16, 23, 255);
        SDL_RenderClear(ren);

        if (running_view && frame) {
            /*
             * Fitted, then filtered according to how it fitted.
             *
             * 4:3 by default, because the Saturn's modes are not square pixels
             * and a stretched picture is simply the wrong one.
             *
             * The filtering is the part worth explaining, and it is lifted
             * from psx-core, which had the same problem: a 320x224 picture in
             * a window that is not a whole multiple of it, blitted with hard
             * pixels, gives some rows one pixel and others two. That reads as
             * faint horizontal banding across the whole screen and it is the
             * commonest way an emulator looks worse than the machine. So
             * nearest is used where it is exactly right -- an integer scale --
             * and bilinear everywhere else.
             */
            const PictureRect r = picture_rect(win_w, win_h, frame_w, frame_h,
                                               cfg.machine);
            SDL_SetTextureScaleMode(frame,
                picture_linear(r, frame_w, frame_h, cfg.machine)
                    ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            const SDL_FRect dst = { r.x, r.y, r.w, r.h };
            SDL_RenderTexture(ren, frame, nullptr, &dst);
        }

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
    }

    lifecycle.set_playing(false);
    lifecycle.detach();
    save_bram();
    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    saturn::save_app_config(cfg_path, cfg);
    /* The clock and the language, so the BIOS does not ask again. */
    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    if (frame) SDL_DestroyTexture(frame);
    if (logo) SDL_DestroyTexture(logo);
    for (auto &kv : art) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    if (art_console) SDL_DestroyTexture(art_console);
    for (Photo &a : art_periph)
        if (a.tex) SDL_DestroyTexture(a.tex);
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
