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
#include "saturn_media.h"
#include "saturn_saf.h"
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
/* Renames main() to SDL_main, which is the symbol SDLActivity looks up on
 * Android. A no-op everywhere else, so there is one main() and one build. */
#include <SDL3/SDL_main.h>

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
#include <map>
#include <set>
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
/*
 * One screen per entry in the rail.
 *
 * These were five faces with the console's five settings pages hidden behind a
 * tab bar inside one of them -- a row of tabs inside a column of buttons, two
 * navigations stacked on top of each other to reach one page. The rail is the
 * navigation now and there is only the one.
 */
enum class Face { Launch, Discs, Downloads, Saves,
                  Input, Picture, Sound, Processor, Drive,
                  About, Setup };

/*
 * The initial of a title, for the A-Z strip.
 *
 * Everything that is not a letter is a '#', which is where numbers, brackets
 * and the occasional Japanese title end up -- one bucket for the awkward few
 * rather than a row of buttons nobody presses. Leading articles are not
 * stripped: a shelf sorted by what is printed on the spine is the shelf people
 * expect, and "The House of the Dead" is filed under T on a real one too.
 */
/*
 * A title reduced to something two catalogues can agree on.
 *
 * A disc on the shelf is called "Daytona USA (US).chd" and the same game in
 * the catalogue is called "Daytona USA". Region tags, punctuation, spacing and
 * case are all noise for the purpose of deciding they are the same game, so
 * they all go, and anything from the first bracket onwards goes with them.
 */
std::string match_key(const std::string &title)
{
    std::string k;
    for (char c : title) {
        if (c == '(' || c == '[') break;
        if (SDL_isalnum((unsigned char)c))
            k.push_back((char)SDL_tolower((unsigned char)c));
    }
    /*
     * And the definite article, wherever the cataloguer put it.
     *
     * A disc is called "The House of the Dead" and the catalogue files it as
     * "House of the Dead, The" -- the same game, two conventions, no match.
     * Stripping a leading or trailing "the" gives both sides the same answer.
     * Only at the ends: the one in the middle of that title is part of it.
     */
    if (k.size() > 5 && k.compare(0, 3, "the") == 0) k.erase(0, 3);
    if (k.size() > 5 && k.compare(k.size() - 3, 3, "the") == 0) k.erase(k.size() - 3);
    return k;
}

/* The kinds of picture the catalogue keeps, in the order the setting uses. */
const char *const kArtKind[]  = { "titles", "box2d", "boxback", "marquee" };
const char *const kArtLabel[] = { "Title screen", "Box, front", "Box, back", "Marquee" };
/* Roughly the shape each kind actually is, so the card fits the picture
 * instead of framing it in empty plate. A title screen is a 4:3 screenshot, a
 * box scan is a tall Saturn case, a marquee is a wide banner. */
const float kArtAspect[]      = { 0.78f, 1.42f, 1.42f, 0.5f };
const int kArtKinds = 4;

/*
 * The path to one kind of picture, derived from the one the catalogue hands
 * out for free.
 *
 * `preview` is always media/titles/<Name>.png, and the server files every
 * other kind under the same name in its own folder. Swapping the folder is
 * therefore enough, and it saves a request per game to find out something we
 * can already work out. `mediaTypes` says which ones exist, so a game with no
 * box scan falls back to the title screen rather than asking for a 404.
 */
std::string art_path_for_kind(const saturn::MediaGame &g, const char *want)
{
    if (g.preview.empty() || !want) return std::string();
    if (g.media_types.find(want) == std::string::npos) return std::string();
    const size_t slash = g.preview.rfind('/');
    if (slash == std::string::npos) return std::string();
    size_t dir = g.preview.rfind('/', slash - 1);
    dir = (dir == std::string::npos) ? 0 : dir + 1;
    return g.preview.substr(0, dir) + want + g.preview.substr(slash);
}

std::string art_path_for(const saturn::MediaGame &g, int kind)
{
    if (g.preview.empty()) return std::string();
    if (kind <= 0 || kind >= kArtKinds) return g.preview;
    const std::string want = kArtKind[kind];
    if (g.media_types.find(want) == std::string::npos) return g.preview;

    const size_t slash = g.preview.rfind('/');
    if (slash == std::string::npos) return g.preview;
    size_t dir = g.preview.rfind('/', slash - 1);
    dir = (dir == std::string::npos) ? 0 : dir + 1;
    return g.preview.substr(0, dir) + want + g.preview.substr(slash);
}

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
    const float square = ImGui::GetFontSize() * 1.7f;
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
enum { kSrcButton = 1 << 0, kSrcTrigger = 1 << 1, kSrcStick = 1 << 2 };
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

    /*
     * A real typeface, at a size that suits the screen it is on.
     *
     * ImGui's built-in font is a 13-pixel bitmap designed for a debug overlay.
     * It is legible on a desktop monitor at arm's length and unreadable on a
     * handheld, which is where most of this application's life is spent. Roboto
     * comes with the ImGui the core already vendors, so this costs a file next
     * to the binary and nothing else.
     *
     * The size follows the display's own scale rather than a constant: the
     * same number of pixels is a comfortable size on a 1080p monitor and a
     * smear on a 400ppi phone.
     */
    {
        float scale = SDL_GetWindowDisplayScale(win);
        if (scale <= 0.0f) scale = 1.0f;
        const float pt = std::clamp(18.0f * scale, 14.0f, 40.0f);
        bool got = false;
        {
            const std::string f = asset_base() + "assets/ui-font.ttf";
            got = ImGui::GetIO().Fonts->AddFontFromFileTTF(f.c_str(), pt) != nullptr;
        }
        if (!got) {
            /* No file: the built-in font, scaled, rather than nothing. */
            ImGui::GetIO().Fonts->AddFontDefault();
            ImGui::GetIO().FontGlobalScale = std::clamp(scale * 1.3f, 1.0f, 2.5f);
        }
        /*
    }
    apply_style();
    {
        float scale = SDL_GetWindowDisplayScale(win);
        if (scale <= 0.0f) scale = 1.0f;
        /*
         * Asking SDL is not enough on its own: a touch device is not
         * enumerated until the first touch has actually happened, so at
         * startup -- which is when the style is set -- a phone reports none.
         * Android is therefore assumed, and the query is what catches a
         * touchscreen on everything else.
         */
        /*
         * Touch targets, where there is no pointer to be precise with.
         *
         * ImGui's defaults are drawn for a mouse: a few pixels of padding
         * round a button is fine when you can put a cursor inside it and
         * nowhere near enough for a thumb.
         *
         * Not gated on Android as such but on there being a touch device,
         * because a Windows tablet and a handheld PC have the same problem and
         * neither is Android.
         */
        bool touch = false;
#if defined(__ANDROID__)
        touch = true;
#else
        int n_touch = 0;
        SDL_free(SDL_GetTouchDevices(&n_touch));
        touch = n_touch > 0;
#endif
        if (touch) {
            ImGuiStyle &st = ImGui::GetStyle();
            st.FramePadding  = ImVec2(14.0f, 12.0f);
            st.ItemSpacing   = ImVec2(12.0f, 10.0f);
            st.ItemInnerSpacing = ImVec2(10.0f, 8.0f);
            st.ScrollbarSize = 26.0f;
            st.GrabMinSize   = 22.0f;
            st.TouchExtraPadding = ImVec2(4.0f, 6.0f);
        }

        /*
         * Scaling last, and the whole block after apply_style().
         *
         * The theme sets its own padding and spacing, so anything set before
         * it was simply overwritten -- which is why the first attempt at
         * thumb-sized buttons changed nothing at all. Scaling has to be last
         * for the same reason: it multiplies whatever is in the style, and a
         * style assigned afterwards is back to unscaled pixels.
         */
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

    saturn::AppConfig cfg;
    if (cfg.disc_root.empty()) {
        /* A sensible first guess, created on demand rather than at startup:
         * an empty folder the user never asked for is litter. */
        if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
            cfg.disc_root = std::string(home) + "Saturn";
    }
    saturn::load_app_config(cfg_path, cfg);

    std::vector<saturn::Game> games = saturn::scan_discs(cfg.disc_root);
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
    };

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
            /* A path, so use it and forget the tree: every part of the app
             * works better with one, and staging would be copying a file onto
             * the device it is already on.
             *
             * Through folder_for, so a library already sitting in "Games" is
             * found rather than a new empty "cd" being made beside it. */
            cfg.disc_tree.clear();
            cfg.disc_root = saturn::folder_for(real, "cd");
            cfg.saves_dir = saturn::folder_for(real, "saves");
            SDL_CreateDirectory(cfg.disc_root.c_str());
            SDL_CreateDirectory(cfg.saves_dir.c_str());
        } else {
            cfg.disc_root.clear();
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
        const size_t slash = cfg.disc_root.rfind('/');
        const std::string parent = slash == std::string::npos
                                 ? cfg.disc_root : cfg.disc_root.substr(0, slash);
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
    std::string saves_dir, states_dir, bram_path;
    auto resolve_saves = [&] {
        saves_dir = cfg.saves_dir;
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
        ymir_bridge_load_internal_backup_memory(ymir, bram_path.c_str(), 0);
    };
    auto save_bram = [&] {
        ymir_bridge_save_internal_backup_memory(ymir, bram_path.c_str());
    };
    load_bram();

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
            disc_path = saturn::saf_stage(d->tree, "cd", d->file, cache);
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
    SDL_Texture *art_pad     = nullptr; int art_pad_w = 0, art_pad_h = 0;
    SDL_Texture *art_gun     = nullptr; int art_gun_w = 0, art_gun_h = 0;

    /* Which photograph belongs to a socket. Anything without one of its own
     * borrows the pad, which is what all of them are shaped like. */
    auto art_for = [&](saturn::Peripheral p, int *w, int *h) -> SDL_Texture * {
        switch (p) {
        case saturn::Peripheral::None:        *w = 0; *h = 0; return nullptr;
        case saturn::Peripheral::VirtuaGun:   *w = art_gun_w; *h = art_gun_h; return art_gun;
        default:                              *w = art_pad_w; *h = art_pad_h; return art_pad;
        }
    };
    {
        const std::string base = asset_base();
        logo = load_png(ren, (base + "assets/wordmark.png").c_str(),
                        &logo_w, &logo_h);
        art_console = load_png(ren, (base + "assets/console.png").c_str(),
                               &art_console_w, &art_console_h);
        art_pad = load_png(ren, (base + "assets/control-pad.png").c_str(),
                           &art_pad_w, &art_pad_h);
        art_gun = load_png(ren, (base + "assets/virtua-gun.png").c_str(),
                           &art_gun_w, &art_gun_h);
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
    /*
     * Covers where covers can exist, names where they cannot.
     *
     * The art comes from RetroMedia, so on a build without that client the
     * grid can only ever be rows of empty plates -- which is a worse shelf
     * than a list, not a better one. Still a button either way.
     */
    bool shelf_grid = saturn::media_available();

    /* ---- RetroMedia ----
     *
     * Signing in buys two things: cover art for anybody, and -- for an
     * administrator -- discs downloaded straight onto the machine. The whole
     * client is asynchronous, so nothing here ever blocks the frame: a
     * media_begin_* call returns at once and the answer turns up in the poll
     * below, whenever it is ready.
     */
    saturn::MediaAccount account;
    std::vector<saturn::MediaGame> catalogue;      /* what the Downloads page shows */
    /*
     * And the whole thing, once, for matching the shelf against.
     *
     * The Downloads list is filtered by whatever letter and search are in
     * force, which makes it useless for answering "is this disc of mine in the
     * catalogue, and what is its cover?" -- so the full list is fetched once
     * at sign-in and left alone.
     */
    std::map<std::string, saturn::MediaGame> by_key;
    bool have_full_catalogue = false;
    std::string media_message, media_email_prefill = saturn::media_last_email();
    char media_email[128] = {0}, media_pass[128] = {0};
    char media_search[96] = {0};
    char media_letter = 0;
    bool media_busy = false;
    bool downloads_offered = false;
    SDL_strlcpy(media_email, media_email_prefill.c_str(), sizeof media_email);
    if (saturn::media_available()) saturn::media_begin_status();

    /*
     * Cover art, fetched as it is needed and kept as textures.
     *
     * A catalogue of two hundred and fifty titles is two hundred and fifty
     * requests if you ask for all of them at once, so art is asked for only
     * for the cards actually on screen, a few at a time, and remembered once
     * it arrives. `asked` is separate from the texture map on purpose: a title
     * with no artwork at all must be asked about once and then left alone,
     * not retried every frame for as long as the page is open.
     */
    struct Art { SDL_Texture *tex = nullptr; int w = 0, h = 0; };
    std::map<std::string, Art> art;
    std::set<std::string> art_asked;
    std::map<std::string, std::string> art_pending;   /* slug -> key in flight */
    int art_in_flight = 0;

    /*
     * One cover, drawn the same on the shelf and in the catalogue.
     *
     * Returns true if the card was clicked. The art request is made here too,
     * so a card that has never been seen asks for its own picture and one that
     * has been answered already does not ask again.
     */
    auto cover_card = [&](const std::string &slug, const std::string &preview,
                          const std::string &title, const char *sub,
                          float card_w, int *budget) -> bool {
        const float cover_h = card_w *
            kArtAspect[std::clamp(cfg.machine.art_kind, 0, kArtKinds - 1)];
        /* Keyed by the picture, not by the game: switching the shelf from
         * title screens to box scans must ask for something new, and it does
         * not if both are filed under the slug. */
        const std::string key = slug + "#" + preview;
        auto it = art.find(key);
        if (it == art.end() && budget && *budget > 0 && art_in_flight < 4 &&
            !slug.empty() && !preview.empty() &&
            art_asked.find(key) == art_asked.end()) {
            art_asked.insert(key);
            art_in_flight++;
            (*budget)--;
            art_pending[slug] = key;
            saturn::media_begin_artwork(slug, preview);
        }

        ImGui::BeginGroup();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(card_w, cover_h));
        const bool hit = ImGui::IsItemClicked();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (it != art.end() && it->second.tex) {
            /* Fitted inside the box, whatever shape the artwork turned out
             * to be -- the catalogue has both title screens and box scans. */
            const Art &a = it->second;
            float w = card_w, h = w * (float)a.h / (float)a.w;
            if (h > cover_h) { h = cover_h; w = h * (float)a.w / (float)a.h; }
            dl->AddImage((ImTextureID)(intptr_t)a.tex,
                         ImVec2(at.x + (card_w - w) * 0.5f, at.y + (cover_h - h) * 0.5f),
                         ImVec2(at.x + (card_w + w) * 0.5f, at.y + (cover_h + h) * 0.5f));
        } else {
            /* A plate where the cover will be, so the grid does not reflow
             * under the pointer as pictures arrive. */
            dl->AddRectFilled(at, ImVec2(at.x + card_w, at.y + cover_h),
                              IM_COL32(28, 32, 44, 255), 4.0f);
            dl->AddRect(at, ImVec2(at.x + card_w, at.y + cover_h),
                        IM_COL32(60, 70, 92, 255), 4.0f);
        }

        /* Two lines for the title, always. Left to wrap freely a long one
         * made its card taller than its neighbours, and a row is as tall as
         * its tallest card, so the whole grid went ragged. */
        ImGui::BeginChild((std::string("t") + slug + title).c_str(),
                          ImVec2(card_w, ImGui::GetTextLineHeight() * 2.2f));
        ImGui::PushTextWrapPos(card_w);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsWindowHovered()) ImGui::SetTooltip("%s", title.c_str());
        ImGui::EndChild();
        if (sub && *sub) TextDim("%s", sub);
        ImGui::EndGroup();
        return hit;
    };

    auto refresh_catalogue = [&] {
        media_busy = true;
        const char one[2] = { media_letter, 0 };
        saturn::media_begin_catalogue(media_search, media_letter ? one : "", true);
    };
    /* The wizard runs until it is finished once, and can be asked for again
     * from Console. A disc on the command line skips it: somebody who
     * double-clicked a game has answered the only question it asks. */
    bool wizard = !cfg.wizard_done && loaded_game_index < 0;
    int  wstep = 0;
    bool running_view = loaded_game_index >= 0;   /* given a disc: play it */
    bool show_pause = false;

    /*
     * The machine only runs while you are looking at it.
     *
     * It used to keep running behind the shelf, which you could hear: a game
     * carried on playing its music while you browsed for another one. It also
     * burned a core for nothing. The pause stops the audio device as well as
     * the emulation, and the mailbox is still served while paused, so swapping
     * a disc or saving a state from the menu still works.
     */
    bool machine_running = running_view;
    ymir_bridge_set_presentation_paused(ymir, machine_running ? 0 : 1);

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

    int  state_slot = 0;
    std::string state_message;
    Uint64 state_message_at = 0;

    auto state_path = [&](int slot) {
        std::string stem = base_name(loaded_path);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.erase(dot);
        for (char &c : stem)
            if (c == '/' || c == '\\' || c == ':') c = '_';
        return states_dir + "/" + stem + ".s" + std::to_string(slot + 1);
    };

    auto say = [&](const char *what, int32_t rc) {
        char buf[160];
        if (rc == YMIR_OK) snprintf(buf, sizeof buf, "%s", what);
        else               snprintf(buf, sizeof buf, "%s failed (%d)", what, rc);
        state_message = buf;
        state_message_at = SDL_GetTicks();
    };

    auto do_save_state = [&](int slot) {
        if (loaded_game_index < 0) return;
        say("State saved", ymir_bridge_save_state(ymir, state_path(slot).c_str()));
    };
    auto do_load_state = [&](int slot) {
        if (loaded_game_index < 0) return;
        if (!path_is_file(state_path(slot))) { say("Slot is empty", -1); return; }
        say("State loaded", ymir_bridge_load_state(ymir, state_path(slot).c_str()));
    };
    char search[96] = {0};
    bool quit = false;

    while (!quit) {
        /* Read once, at the top: the events below need it too -- a touch
         * arrives in normalised coordinates and has nothing to scale by
         * otherwise. */
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(win, &win_w, &win_h);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) quit = true;

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
                    do_save_state(state_slot);
                } else if (down && ev.key.scancode == SDL_SCANCODE_F8) {
                    do_load_state(state_slot);
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
                TextDimWrapped("The emulation is Ymir's work, under the GPL. This app "
                               "supplies no BIOS and no games.");
            }

            else if (wstep == 1) {
                ImGui::TextWrapped("Where do you keep your disc images?");
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
                                   "app makes three folders and touches nothing else: "
                                   "bios for your Saturn BIOS, cd for your discs, and "
                                   "saves for your game saves. Folders already there "
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
                            adopt_tree(t.uri);
                            resolve_saves();
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
                                                  cfg.disc_root == r)) {
                        cfg.disc_tree.clear();
                        cfg.disc_root = r;
                        SDL_CreateDirectory(r.c_str());
                        resolve_saves();
                        rescan();
                        adopt_bios_from_discs();
                        saturn::save_app_config(cfg_path, cfg);
                    }
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", r.c_str());
                    ImGui::PopID();
                }
#else
                TextDimWrapped("One folder of disc images. Not a folder of folders: "
                               "a Saturn game is a disc.");
                ImGui::Spacing();
                for (const std::string &r : saturn::candidate_disc_roots()) {
                    ImGui::PushID(r.c_str());
                    if (ImGui::RadioButton("##r", cfg.disc_root == r)) {
                        cfg.disc_root = r;
                        SDL_CreateDirectory(r.c_str());
                        resolve_saves();
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
                if (ImGui::Button("Choose another folder...")) saturn::begin_pick_folder();
                ImGui::EndDisabled();
                {
                    std::string got;
                    if (saturn::take_pick(got)) {
                        cfg.disc_root = got;
                        resolve_saves();
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
                    if (ImGui::Button("Choose the BIOS file...")) saturn::begin_pick_file();
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

            else if (wstep == 3) {
                const std::string demo = saturn::demo_disc_path();
                ImGui::Spacing();
                if (demo.empty()) {
                    /* Not an apology for a missing file: the phone build
                     * leaves it out on purpose, and saying which build you are
                     * on is more use than saying something is absent. */
                    ImGui::TextWrapped("Nothing is bundled to run, so this step is "
                                       "just to say what happens next.");
                    ImGui::Spacing();
                    TextDimWrapped("The demo disc is 83 MB of mostly CD audio and it "
                                   "would be the whole of this download, so it ships "
                                   "with the desktop build and not this one. Put your "
                                   "own discs in the folder from the last step and "
                                   "they appear on the shelf.");
                } else {
                    ImGui::TextWrapped("A demo is included, so there is something to "
                                       "run before you have copied anything across.");
                    ImGui::Spacing();
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
            const int n_tabs = downloads_offered ? 10 : 9;

            /* ============ the rail, where there is width for one ============ */
            if (!narrow) {
                ImGui::BeginChild("##rail", ImVec2(rail_w, body_h));
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
                tab("Launch", Face::Launch, bsz);
                ImGui::Spacing();

                tab("Discs", Face::Discs, bsz);
                if (downloads_offered) tab("Downloads", Face::Downloads, bsz);
                tab("Save states", Face::Saves, bsz);

                ImGui::Spacing();
                TextDim("  MACHINE");
                tab("Input", Face::Input, bsz);
                tab("Picture", Face::Picture, bsz);
                tab("Sound", Face::Sound, bsz);
                tab("Processor", Face::Processor, bsz);
                tab("Disc drive", Face::Drive, bsz);

                ImGui::Spacing();
                tab("About", Face::About, bsz);
                tab("Setup", Face::Setup, bsz);
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
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                /* Two lines of title, a line for a message, and the button,
                 * which is taller than an ordinary frame. Measured, because
                 * the first version left Power on half off the bottom. */
                const float text_h = ImGui::GetTextLineHeightWithSpacing() * 3.0f
                                   + fs * 2.2f
                                   + ImGui::GetStyle().ItemSpacing.y * 4.0f;
                const float stage_h = std::max(avail.y - text_h, fs * 6.0f);

                /* The console, as big as the stage allows, with room beside it
                 * for the disc. */
                float ch = stage_h * 0.72f;
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

                /* Under the console, under its ports. */
                const float ph = std::max(stage_h - ch - ImGui::GetStyle().ItemSpacing.y,
                                          ImGui::GetFrameHeight() * 0.8f);
                const float half_w = (cwid - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                for (int which = 1; which <= 2; ++which) {
                    const saturn::Peripheral p = (which == 1) ? cfg.machine.port1
                                                              : cfg.machine.port2;
                    if (which == 2) ImGui::SameLine();
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
                    } else {
                        /* An empty socket, drawn as one: a dark slot of the
                         * shape the Saturn's actually are. */
                        const float sw = std::min(half_w * 0.5f, ph * 1.7f);
                        const float sh = std::min(ph * 0.42f, sw * 0.34f);
                        const ImVec2 a(at.x + (half_w - sw) * 0.5f,
                                       at.y + (ph - sh) * 0.5f);
                        const ImVec2 b(a.x + sw, a.y + sh);
                        dl->AddRectFilled(a, b, IM_COL32(10, 11, 16, 255), 3.0f);
                        dl->AddRect(a, b, IM_COL32(64, 72, 94, 220), 3.0f);
                    }
                }
                ImGui::EndGroup();

                /* ---- the disc, turning ---- */
                if (loaded_game_index >= 0) {
                    ImGui::SameLine(0.0f, gap);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(disc_d, stage_h));
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    const ImVec2 c(at.x + disc_d * 0.5f, at.y + stage_h * 0.5f);
                    const float r = disc_d * 0.5f;

                    /*
                     * Always turning while it is on screen.
                     *
                     * It used to stop whenever the machine was paused, which
                     * was right when it lived behind a frozen picture -- and
                     * wrong here, because the machine is always paused on this
                     * page and a disc that never moves just looks broken.
                     */
                    static float spin = 0.0f;
                    spin += ImGui::GetIO().DeltaTime * 2.4f;

                    SDL_Texture *disc_tex = nullptr;
                    {
                        auto m = by_key.find(match_key(games[loaded_game_index].title));
                        if (m != by_key.end() &&
                            m->second.media_types.find("cartridges") != std::string::npos) {
                            const std::string pth = art_path_for_kind(m->second, "cartridges");
                            const std::string k = m->second.slug + "#" + pth;
                            auto a = art.find(k);
                            if (a != art.end()) disc_tex = a->second.tex;
                            else if (!pth.empty() && art_asked.find(k) == art_asked.end()) {
                                art_asked.insert(k);
                                art_pending[m->second.slug] = k;
                                art_in_flight++;
                                saturn::media_begin_artwork(m->second.slug, pth);
                            }
                        }
                    }

                    if (disc_tex) {
                        /* Four corners turned about the middle: ImGui has no
                         * rotated image, but a quad with rotated corners is
                         * the same thing. */
                        const float cs = cosf(spin), sn = sinf(spin);
                        auto turn = [&](float dx, float dy) {
                            return ImVec2(c.x + dx * cs - dy * sn, c.y + dx * sn + dy * cs);
                        };
                        dl->AddImageQuad((ImTextureID)(intptr_t)disc_tex,
                                         turn(-r, -r), turn(r, -r), turn(r, r), turn(-r, r),
                                         ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1));
                        dl->AddCircle(c, r * 0.99f, IM_COL32(150, 170, 200, 70), 64, 1.5f);
                    } else {
                        dl->AddCircleFilled(c, r, IM_COL32(24, 27, 38, 255), 64);
                        /* The sheen: spokes of shifting hue, which is what a
                         * CD does under a light. */
                        for (int i = 0; i < 12; ++i) {
                            const float a = spin + (float)i * 6.2831853f / 12.0f;
                            float cr, cg, cb;
                            ImGui::ColorConvertHSVtoRGB((float)i / 12.0f, 0.55f, 1.0f,
                                                        cr, cg, cb);
                            const ImU32 col = IM_COL32((int)(cr * 255), (int)(cg * 255),
                                                       (int)(cb * 255), 70);
                            dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.34f,
                                               c.y + sinf(a) * r * 0.34f),
                                        ImVec2(c.x + cosf(a) * r * 0.97f,
                                               c.y + sinf(a) * r * 0.97f),
                                        col, r * 0.20f);
                        }
                        dl->AddCircle(c, r * 0.985f, IM_COL32(150, 170, 200, 90), 64, 1.5f);
                        dl->AddCircleFilled(c, r * 0.30f, IM_COL32(14, 16, 23, 255), 48);
                        dl->AddCircle(c, r * 0.30f, IM_COL32(150, 170, 200, 110), 48, 1.5f);
                        dl->AddCircleFilled(c, r * 0.12f, IM_COL32(9, 10, 15, 255), 32);
                    }
                }

                /* ---- what is in it, and the button ---- */
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.71f, 0.97f, 1.0f));
                ImGui::TextUnformatted(loaded_title.empty() ? "NO DISC"
                                                            : loaded_title.c_str());
                ImGui::PopStyleColor();
                if (!loaded_title.empty()) TextDim("%s", base_name(loaded_path).c_str());
                else TextDim("The tray is empty. Choose one from Discs.");
                if (!message.empty()) TextDim("%s", message.c_str());

                ImGui::Spacing();
                ImGui::BeginDisabled(loaded_game_index < 0);
                if (ImGui::Button("Power on", ImVec2(fs * 11.0f, fs * 2.2f))) {
                    running_view = true;
                    show_pause = false;
                }
                ImGui::EndDisabled();

                /* The disc selector, only for games that came on more than
                 * one. A single-disc game showing "Disc 1 of 1" is noise. */
                if (loaded_game_index >= 0 && games[loaded_game_index].multi()) {
                    ImGui::SameLine();
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
                if (downloads_offered) flow("Downloads", Face::Downloads);
                flow("Saves", Face::Saves);
                flow("Input", Face::Input);
                flow("Picture", Face::Picture);
                flow("Sound", Face::Sound);
                flow("Processor", Face::Processor);
                flow("Disc drive", Face::Drive);
                flow("About", Face::About);
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
                ImGui::SetNextItemWidth(fw * 0.5f);
                ImGui::InputTextWithHint("##find", "Find a game...", search,
                                         sizeof search);
                ImGui::SameLine();
                if (ImGui::Button("Rescan")) rescan();
                ImGui::SameLine();
                if (ImGui::Button(shelf_grid ? "List" : "Covers")) shelf_grid = !shelf_grid;
                if (shelf_grid && !by_key.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Title screen").x +
                                            ImGui::GetFrameHeight() * 1.6f);
                    if (ImGui::BeginCombo("##artkind",
                                          kArtLabel[std::clamp(cfg.machine.art_kind,
                                                               0, kArtKinds - 1)])) {
                        for (int k = 0; k < kArtKinds; ++k)
                            if (ImGui::Selectable(kArtLabel[k], cfg.machine.art_kind == k)) {
                                cfg.machine.art_kind = k;
                                saturn::save_app_config(cfg_path, cfg);
                            }
                        ImGui::EndCombo();
                    }
                }
                /* On its own line. Beside the buttons it was the first thing
                 * to be cut off when the font grew, and a path that ends in
                 * "(no folder s" is worse than no path at all. */
                TextDimWrappedF("%zu game%s in %s", games.size(),
                                games.size() == 1 ? "" : "s",
                                !cfg.disc_tree.empty()
                                    ? saturn::saf_folder_name(cfg.disc_tree, "cd").c_str()
                                : cfg.disc_root.empty() ? "(no folder set)"
                                                        : cfg.disc_root.c_str());
                ImGui::Separator();

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
                                   ".ccd files in the folder above -- one game per "
                                   "disc, and a game that came on several discs will "
                                   "be grouped back together by its name.");
                } else if (shelf_grid) {
                    /*
                     * The shelf as covers.
                     *
                     * The pictures are the catalogue's, matched to the discs
                     * by a normalised title -- so a signed-in account gets
                     * artwork for games it already owns without downloading
                     * anything. Without a match the card is a plate with the
                     * name on it, which is still a shelf.
                     */
                    ImGui::BeginChild("##shelf");
                    const float card_w = em * 9.0f;
                    const float step = card_w + ImGui::GetStyle().ItemSpacing.x;
                    const int per_row = std::max(1, (int)(ImGui::GetContentRegionAvail().x / step));
                    int budget = 3;
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

                        std::string slug, preview;
                        auto m = by_key.find(match_key(g.title));
                        if (m != by_key.end()) {
                            slug = m->second.slug;
                            preview = art_path_for(m->second, cfg.machine.art_kind);
                        }

                        char sub[48];
                        if (g.multi()) snprintf(sub, sizeof sub, "%zu discs", g.discs.size());
                        else           snprintf(sub, sizeof sub, "%s",
                                                (int)i == loaded_game_index ? "in the drive" : "");
                        if (cover_card(slug.empty() ? g.title : slug, preview,
                                       g.title, sub, card_w, &budget)) {
                            insert_disc((int)i, 0);
                            /* Straight to the machine: choosing a disc and
                             * then having to find the way to the button that
                             * starts it is a step nobody wants. */
                            if (loaded_game_index >= 0) face = Face::Launch;
                        }
                        ImGui::PopID();
                    }
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
                        if (in_drive) ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.42f, 0.71f, 0.97f, 1.0f));
                        if (ImGui::Selectable(g.title.c_str(), in_drive, 0,
                                              ImVec2(0, fs * 1.9f))) {
                            insert_disc((int)i, 0);
                            if (loaded_game_index >= 0) face = Face::Launch;
                        }
                        if (in_drive) ImGui::PopStyleColor();
                        ImGui::SameLine(fw * 0.66f);
                        if (g.multi()) TextDim("%zu discs", g.discs.size());
                        else           TextDim("%s", g.discs[0].file.c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
            }

            else if (face == Face::Downloads) {
                ImGui::Spacing();
                TextDim("%s   %d credit%s, %d free left", account.email.c_str(),
                        account.credits, account.credits == 1 ? "" : "s",
                        account.free_remaining);
                ImGui::SameLine(fw - fs * 7.0f);
                if (ImGui::Button("Sign out")) {
                    saturn::media_begin_logout();
                    catalogue.clear();
                }

                ImGui::Spacing();
                ImGui::SetNextItemWidth(fw * 0.45f);
                if (ImGui::InputTextWithHint("##dlsearch", "Find a game...",
                                             media_search, sizeof media_search,
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                    refresh_catalogue();
                ImGui::SameLine();
                if (ImGui::Button("Search")) refresh_catalogue();
                ImGui::SameLine();
                if (ImGui::Button("Refresh")) {
                    /* Forget the art as well as the list: a cover that failed
                     * once is otherwise never asked for again. */
                    for (auto &kv : art) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
                    art.clear();
                    art_asked.clear();
                    art_in_flight = 0;
                    refresh_catalogue();
                }
                ImGui::SameLine();
                TextDim(media_busy ? "working..." : "%zu title%s", catalogue.size(),
                        catalogue.size() == 1 ? "" : "s");

                /*
                 * The whole alphabet here, not only the letters in view.
                 *
                 * The shelf offers the initials it has, because it has the
                 * whole shelf in front of it. The catalogue is on the other
                 * end of a network and is paged, so what is on this page says
                 * nothing about what exists -- pressing S has to be able to
                 * ask the server for S.
                 */
                ImGui::Spacing();
                {
                    std::string all = "#";
                    for (char c = 'A'; c <= 'Z'; ++c) all.push_back(c);
                    if (letter_strip(all, media_letter, fw)) refresh_catalogue();
                }

                ImGui::Spacing();
                ImGui::Separator();
                const std::string prog = saturn::media_progress();
                if (!prog.empty()) {
                    ImGui::TextUnformatted(prog.c_str());
                } else if (!media_message.empty()) {
                    TextDim("%s", media_message.c_str());
                }

                /*
                 * A grid of covers, not a list of lines.
                 *
                 * Two hundred and fifty rows of text is a spreadsheet, and
                 * nobody recognises a game from its name in a column. The
                 * cards are sized off the text so they grow with the font and
                 * the display, and the art is asked for lazily as they come
                 * into view.
                 */
                ImGui::BeginChild("##dl");
                {
                    const float card_w = em * 9.0f;
                    const float step = card_w + ImGui::GetStyle().ItemSpacing.x;
                    const int per_row = std::max(1, (int)(ImGui::GetContentRegionAvail().x / step));
                    int budget = 3;          /* new art requests this frame */

                    for (size_t i = 0; i < catalogue.size(); ++i) {
                        const saturn::MediaGame &g = catalogue[i];
                        if ((int)(i % per_row) != 0) ImGui::SameLine();
                        ImGui::PushID((int)i);
                        char sub[32];
                        if (g.bytes > 0) snprintf(sub, sizeof sub, "%.0f MB",
                                                  (double)g.bytes / 1048576.0);
                        else             snprintf(sub, sizeof sub, "%d file%s",
                                                  g.rom_files, g.rom_files == 1 ? "" : "s");
                        ImGui::BeginGroup();
                        cover_card(g.slug, art_path_for(g, cfg.machine.art_kind),
                                   g.title, sub, card_w, &budget);
                        ImGui::BeginDisabled(!prog.empty() || cfg.disc_root.empty());
                        if (ImGui::Button("Download", ImVec2(card_w, 0)))
                            saturn::media_begin_download(g.slug, cfg.disc_root);
                        ImGui::EndDisabled();
                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }

            else if (face == Face::Saves) {
                /*
                 * What is on disk, and a way to get rid of it.
                 *
                 * Save data is the one thing in this application that cannot
                 * be downloaded again, so it should be possible to see what
                 * there is, where it is, and how old it is -- without going
                 * and finding a file manager.
                 */
                ImGui::Spacing();
                ImGui::TextUnformatted("Where saves are kept");
                TextDim("%s", saves_dir.c_str());
                TextDimWrapped("Outside the app, so an uninstall or a new build "
                               "cannot take them with it. Change it in Console, "
                               "Setup.");

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("The Saturn's memory");
                {
                    SDL_PathInfo info;
                    if (SDL_GetPathInfo(bram_path.c_str(), &info) &&
                        info.type == SDL_PATHTYPE_FILE) {
                        SDL_DateTime dt{};
                        if (SDL_TimeToDateTime((SDL_Time)info.modify_time, &dt, true))
                            TextDim("backup-ram.bin  %llu bytes  last written "
                                    "%04d-%02d-%02d %02d:%02d",
                                    (unsigned long long)info.size,
                                    dt.year, dt.month, dt.day, dt.hour, dt.minute);
                        else
                            TextDim("backup-ram.bin  %llu bytes",
                                    (unsigned long long)info.size);
                    } else {
                        TextDim("not written yet");
                    }
                    TextDimWrapped("The 32 KiB battery-backed memory inside the "
                                   "console. Every game that saves anything saves it "
                                   "here, all of them sharing the one chip, exactly "
                                   "as on the hardware.");
                    if (ImGui::Button("Keep a dated copy")) {
                        SDL_DateTime dt{};
                        SDL_Time now = 0;
                        SDL_GetCurrentTime(&now);
                        SDL_TimeToDateTime(now, &dt, true);
                        char stamp[64];
                        snprintf(stamp, sizeof stamp,
                                 "%s/backup-ram-%04d%02d%02d-%02d%02d%02d.bin",
                                 saves_dir.c_str(), dt.year, dt.month, dt.day,
                                 dt.hour, dt.minute, dt.second);
                        say("Copy kept", ymir_bridge_save_internal_backup_memory(
                                             ymir, stamp));
                    }
                    ImGui::SameLine();
                    TextDim("before trying something that might overwrite it");
                }

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextUnformatted("Save states");
                {
                    int n = 0;
                    char **found = SDL_GlobDirectory(states_dir.c_str(), "*", 0, &n);
                    if (n <= 0) {
                        TextDimWrapped("None yet. In a game, press Escape and use the "
                                       "slots there, or F5 to save and F8 to load.");
                    }
                    ImGui::BeginChild("##states");
                    for (int i = 0; found && i < n; ++i) {
                        const std::string name = found[i];
                        const std::string full = states_dir + "/" + name;
                        SDL_PathInfo info;
                        if (!SDL_GetPathInfo(full.c_str(), &info) ||
                            info.type != SDL_PATHTYPE_FILE) continue;
                        ImGui::PushID(i);
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::SameLine(fw * 0.50f);
                        SDL_DateTime dt{};
                        if (SDL_TimeToDateTime((SDL_Time)info.modify_time, &dt, true))
                            TextDim("%04d-%02d-%02d %02d:%02d   %.1f MB",
                                    dt.year, dt.month, dt.day, dt.hour, dt.minute,
                                    (double)info.size / (1024.0 * 1024.0));
                        ImGui::SameLine(fw * 0.84f);
                        /* Held, not clicked: deleting a save by brushing past
                         * the wrong row is not a mistake worth allowing. */
                        ImGui::SmallButton("Delete");
                        if (ImGui::IsItemActive() &&
                            ImGui::GetIO().MouseDownDuration[0] > 0.6f) {
                            SDL_RemovePath(full.c_str());
                            say("Deleted", YMIR_OK);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Hold to delete");
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (found) SDL_free(found);
                }
                if (!state_message.empty() &&
                    SDL_GetTicks() - state_message_at <= 5000)
                    ImGui::TextUnformatted(state_message.c_str());
            }

            else if (face == Face::Setup || face == Face::Input ||
                     face == Face::Picture || face == Face::Sound ||
                     face == Face::Processor || face == Face::Drive) {
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
                    if (face == Face::Setup) {
                        ImGui::Spacing();
                        col_begin("##setupcols");
                        ImGui::TextUnformatted("BIOS");
                        TextDimWrapped("The Saturn will not start without one, and this "
                                       "app does not include it -- it is Sega's. Point "
                                       "this at the ROM you dumped from your own "
                                       "console.");
                        static char bios_buf[1024];
                        static bool primed = false;
                        if (!primed) {
                            SDL_strlcpy(bios_buf, cfg.bios_path.c_str(), sizeof bios_buf);
                            primed = true;
                        }
                        /* In one column the field and its button sit on a
                         * line together; in two there is no room, and the
                         * button went off the edge of the column entirely. */
                        ImGui::SetNextItemWidth(two_col ? -FLT_MIN : fw * 0.6f);
                        if (ImGui::InputText("##bios", bios_buf, sizeof bios_buf)) {
                            cfg.bios_path = bios_buf;
                            load_bios();
                            saturn::save_app_config(cfg_path, cfg);
                        }
                        if (saturn::pickers_usable()) {
                            if (!two_col) ImGui::SameLine();
                            ImGui::BeginDisabled(saturn::pick_in_progress());
                            if (ImGui::Button("Browse...")) saturn::begin_pick_file();
                            ImGui::EndDisabled();
                        } else if (ImGui::Button("Find the BIOS")) {
                            const std::string b = saturn::find_bios_in(cfg.disc_root);
                            if (!b.empty()) {
                                cfg.bios_path = b;
                                SDL_strlcpy(bios_buf, b.c_str(), sizeof bios_buf);
                                load_bios();
                                saturn::save_app_config(cfg_path, cfg);
                            }
                        }
                        if (bios_loaded) {
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                            ImGui::TextUnformatted("loaded");
                            ImGui::PopStyleColor();
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                            ImGui::TextUnformatted(cfg.bios_path.empty()
                                                   ? "not set" : "not readable");
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
                        ImGui::SetNextItemWidth(two_col ? -FLT_MIN : fw * 0.6f);
                        if (ImGui::InputText("##root", root_buf, sizeof root_buf)) {
                            cfg.disc_root = root_buf;
                            saturn::save_app_config(cfg_path, cfg);
                            rescan();
                        }
                        if (saturn::pickers_usable()) {
                            if (!two_col) ImGui::SameLine();
                            ImGui::BeginDisabled(saturn::pick_in_progress());
                            if (ImGui::Button("Browse...##root")) saturn::begin_pick_folder();
                            ImGui::EndDisabled();
                        } else {
                            /* The folders that need no permission, as buttons.
                             * Typing a path on a phone keyboard is not a route
                             * anybody should have to take. */
                            for (const std::string &r : saturn::candidate_disc_roots()) {
                                ImGui::PushID(r.c_str());
                                if (ImGui::Button(r.c_str())) {
                                    cfg.disc_tree.clear();
                                    cfg.disc_root = r;
                                    SDL_strlcpy(root_buf, r.c_str(), sizeof root_buf);
                                    SDL_CreateDirectory(r.c_str());
                                    rescan();
                                    adopt_bios_from_discs();
                                    SDL_strlcpy(bios_buf, cfg.bios_path.c_str(),
                                                sizeof bios_buf);
                                    saturn::save_app_config(cfg_path, cfg);
                                }
                                ImGui::PopID();
                            }
                            if (saturn::saf_available()) {
                                ImGui::Spacing();
                                if (ImGui::Button("Choose a folder...")) saturn::saf_pick();
                                TextDimWrapped("Anywhere on the device or a memory "
                                               "card. This app makes bios, cd and "
                                               "saves inside whatever you choose, and "
                                               "uses nothing else.");
                                for (const saturn::SafTree &t : saturn::saf_trees()) {
                                    ImGui::PushID(t.uri.c_str());
                                    const bool in_use =
                                        t.uri == cfg.disc_tree ||
                                        (!t.path.empty() &&
                                         cfg.disc_root.rfind(t.path, 0) == 0);
                                    if (in_use) {
                                        TextDimWrappedF("using %s", t.name.c_str());
                                    } else if (ImGui::Button(t.name.c_str())) {
                                        adopt_tree(t.uri);
                                        SDL_strlcpy(root_buf, cfg.disc_root.c_str(),
                                                    sizeof root_buf);
                                        resolve_saves();
                                        rescan();
                                        adopt_bios_from_discs();
                                        SDL_strlcpy(bios_buf, cfg.bios_path.c_str(),
                                                    sizeof bios_buf);
                                    }
                                    ImGui::PopID();
                                }
                            }
                        }

                        col_next();
                        ImGui::TextUnformatted("RetroMedia");
                        if (!saturn::media_available()) {
                            TextDimWrapped("Not built into this version.");
                        } else if (account.signed_in) {
                            TextDim("Signed in as %s%s", account.email.c_str(),
                                    account.is_admin ? " (administrator)" : "");
                            TextDimWrapped(account.is_admin
                                ? "The Downloads tab is yours."
                                : "Cover art only -- downloading discs needs an "
                                  "administrator account.");
                            if (ImGui::Button("Sign out##setup")) {
                                saturn::media_begin_logout();
                                catalogue.clear();
                            }
                        } else {
                            TextDimWrapped("An account at "
                                           "media.crownparkcomputing.com. An ordinary "
                                           "email and password -- there is no Google "
                                           "account involved.");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::InputTextWithHint("##email", "email",
                                                     media_email, sizeof media_email);
                            ImGui::SetNextItemWidth(
                                two_col ? -(ImGui::CalcTextSize("Sign in").x +
                                            ImGui::GetStyle().FramePadding.x * 2.0f +
                                            ImGui::GetStyle().ItemSpacing.x)
                                        : fw * 0.42f);
                            const bool enter = ImGui::InputTextWithHint(
                                "##pass", "password", media_pass, sizeof media_pass,
                                ImGuiInputTextFlags_Password |
                                ImGuiInputTextFlags_EnterReturnsTrue);
                            ImGui::SameLine();
                            ImGui::BeginDisabled(media_busy || !media_email[0] ||
                                                 !media_pass[0]);
                            const bool go = ImGui::Button("Sign in");
                            ImGui::EndDisabled();
                            if ((enter || go) && media_email[0] && media_pass[0]) {
                                media_busy = true;
                                saturn::media_begin_login(media_email, media_pass);
                                /* The password leaves this buffer the moment the
                                 * request has it, and again when the answer
                                 * arrives. It is never written anywhere. */
                                SDL_memset(media_pass, 0, sizeof media_pass);
                            }
                        }
                        if (!media_message.empty()) TextDim("%s", media_message.c_str());

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextUnformatted("Saves folder");
                        TextDimWrapped("The Saturn's battery memory and the save "
                                       "states. Kept outside the app on purpose: "
                                       "anything inside it is deleted when the app is "
                                       "uninstalled, and this is the one folder you "
                                       "would not want to lose. Leave it empty for a "
                                       "Saves folder beside the discs.");
                        static char saves_buf[1024];
                        static bool saves_primed = false;
                        if (!saves_primed) {
                            SDL_strlcpy(saves_buf, cfg.saves_dir.c_str(), sizeof saves_buf);
                            saves_primed = true;
                        }
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##saves", saves_buf, sizeof saves_buf)) {
                            cfg.saves_dir = saves_buf;
                            saturn::save_app_config(cfg_path, cfg);
                            save_bram();        /* out of the old place... */
                            resolve_saves();
                            load_bram();        /* ...and into the new one */
                        }
                        TextDim("%s", saves_dir.c_str());

                        {   /* Whichever dialog was opened, its answer lands here. */
                            std::string got;
                            if (saturn::take_pick(got)) {
                                SDL_PathInfo pi;
                                const bool isdir = SDL_GetPathInfo(got.c_str(), &pi) &&
                                                   pi.type == SDL_PATHTYPE_DIRECTORY;
                                if (isdir) {
                                    cfg.disc_root = got;
                                    SDL_strlcpy(root_buf, got.c_str(), sizeof root_buf);
                                    rescan();
                                } else {
                                    cfg.bios_path = got;
                                    SDL_strlcpy(bios_buf, got.c_str(), sizeof bios_buf);
                                    load_bios();
                                }
                                saturn::save_app_config(cfg_path, cfg);
                            }
                        }

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
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
                        col_end();
                    }

                    if (face == Face::Input) {
                        /*
                         * The two sockets, on a page of their own.
                         *
                         * They used to sit in the header beside the machine,
                         * which put a control you change once beside the disc
                         * you change constantly. Here there is room to say
                         * what each peripheral is as well as name it.
                         */
                        ImGui::Spacing();
                        col_begin("##inputcols");
                        for (int which = 1; which <= 2; ++which) {
                            saturn::Peripheral &p = (which == 1) ? s.port1 : s.port2;
                            ImGui::PushID(which);
                            ImGui::Text("Port %d", which);
                            ImGui::Spacing();

                            int aw = 0, ah = 0;
                            if (SDL_Texture *tex = art_for(p, &aw, &ah)) {
                                const float w = std::min(fw * (two_col ? 0.40f : 0.32f),
                                                         em * 9.0f);
                                ImGui::Image((ImTextureID)(intptr_t)tex,
                                             ImVec2(w, w * (float)ah / (float)aw));
                            }
                            ImGui::Spacing();

                            const int last = (int)saturn::Peripheral::ShuttleMouse;
                            auto step = [&](int by) {
                                p = (saturn::Peripheral)(((int)p + by + last + 1) % (last + 1));
                                apply_ports();
                                saturn::save_app_config(cfg_path, cfg);
                            };
                            if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) step(-1);
                            ImGui::SameLine();
                            ImGui::TextUnformatted(saturn::peripheral_name(p));
                            ImGui::SameLine();
                            if (ImGui::ArrowButton("##next", ImGuiDir_Right)) step(+1);

                            TextDimWrapped(
                                p == saturn::Peripheral::None         ? "Nothing plugged in." :
                                p == saturn::Peripheral::ControlPad   ? "The pad the console came with. A gamepad maps straight onto it." :
                                p == saturn::Peripheral::AnalogPad    ? "The 3D Control Pad: an analogue stick and two analogue triggers." :
                                p == saturn::Peripheral::ArcadeRacer  ? "A wheel. Steering is analogue; the pedals are the triggers." :
                                p == saturn::Peripheral::MissionStick ? "A flight stick, with a throttle." :
                                p == saturn::Peripheral::VirtuaGun    ? "A light gun. The mouse aims and a finger aims; left fires, right reloads, middle is Start." :
                                                                        "The Shuttle Mouse. The mouse moves it.");
                            ImGui::PopID();
                            if (which == 1) col_next();
                        }
                        col_end();

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        if (pads.empty()) {
                            TextDimWrapped("No gamepad found -- keyboard only.");
                        } else {
                            TextDimWrappedF("%zu gamepad%s: %s", pads.size(),
                                            pads.size() == 1 ? "" : "s",
                                            SDL_GetGamepadName(pads[0])
                                                ? SDL_GetGamepadName(pads[0]) : "unnamed");
                        }
                        TextDimWrapped("On a modern pad the bumpers are the Saturn's L "
                                       "and R, and the triggers are the pedals: right "
                                       "accelerates, left brakes. C and Z are the stick "
                                       "clicks.");
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
                        ImGui::Spacing();
                        col_begin("##picturecols");
                        ImGui::TextUnformatted("Smoothing");
                        {
                            int sc = s.scaling;
                            dirty |= ImGui::RadioButton("Automatic", &sc, 0);
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Sharp", &sc, 1);
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Smooth", &sc, 2);
                            s.scaling = sc;
                        }
                        TextDimWrapped("Automatic keeps hard pixels when the window is "
                                       "a whole multiple of the picture and smooths it "
                                       "when it is not. At an awkward size, hard pixels "
                                       "make some rows a pixel taller than others, "
                                       "which shows as faint banding across the "
                                       "screen.");

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextUnformatted("Shape");
                        {
                            int as_ = s.aspect;
                            dirty |= ImGui::RadioButton("4:3", &as_, 0);
                            ImGui::SameLine();
                            dirty |= ImGui::RadioButton("Fill the window", &as_, 1);
                            s.aspect = as_;
                        }
                        TextDimWrapped("4:3 is the shape a Saturn was drawn for. Its "
                                       "modes are not square pixels, so filling the "
                                       "window stretches them.");
                        ImGui::Spacing();
                        dirty |= ImGui::Checkbox("Whole-number scaling",
                                                 &s.integer_scale);
                        TextDimWrapped("Every pixel exactly the same size, with a "
                                       "border where the window does not divide "
                                       "evenly. It overrides the shape above -- 320x224 "
                                       "is not 4:3 -- so it trades the right geometry "
                                       "for the right pixels.");

                        col_next();
                        ImGui::TextUnformatted("Light gun crosshair");
                        ImGui::SetNextItemWidth(fw * (two_col ? 0.40f : 0.45f));
                        dirty |= ImGui::SliderInt("##crosshair", &s.crosshair,
                                                  50, 600, "%d%%");
                        TextDimWrapped("Only drawn when a port is holding a Virtua "
                                       "Gun.");

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextUnformatted("Rendering");
                        dirty |= ImGui::Checkbox("Threaded VDP1", &s.threaded_vdp1);
                        dirty |= ImGui::Checkbox("Threaded VDP2", &s.threaded_vdp2);
                        dirty |= ImGui::Checkbox("Threaded deinterlacer",
                                                 &s.threaded_deinterlace);
                        TextDimWrapped("The two video processors are most of this "
                                       "emulator's work, so they run on their own "
                                       "threads. Turn one off only to find out whether "
                                       "it is the cause of something.");
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

            }   /* everything that is not Launch */
            ImGui::EndChild();     /* ##right */

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
                TextDim("Controllers");
                /* Here as well as on the shelf, because the moment you find
                 * out you started with the wrong one is the moment the game
                 * asks you to press something and nothing happens -- and
                 * going back to the shelf to fix it costs you where you were. */
                {
                    auto pick = [&](const char *label, saturn::Peripheral &p) {
                        int aw = 0, ah = 0;
                        if (SDL_Texture *tex = art_for(p, &aw, &ah)) {
                            const float h = ImGui::GetFrameHeight();
                            ImGui::Image((ImTextureID)(intptr_t)tex,
                                         ImVec2(h * (float)aw / (float)ah, h));
                            ImGui::SameLine();
                        }
                        ImGui::SetNextItemWidth(bw.x - ImGui::GetFontSize() * 2.6f);
                        if (ImGui::BeginCombo(label, saturn::peripheral_name(p))) {
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
                    };
                    pick("##pp1", cfg.machine.port1);
                    pick("##pp2", cfg.machine.port2);
                }

                ImGui::Separator();
                TextDim("Save state");
                for (int sl = 0; sl < 4; ++sl) {
                    ImGui::PushID(100 + sl);
                    char lbl[16];
                    snprintf(lbl, sizeof lbl, "%d", sl + 1);
                    if (ImGui::RadioButton(lbl, state_slot == sl)) state_slot = sl;
                    if (sl < 3) ImGui::SameLine();
                    ImGui::PopID();
                }
                /* Whether the chosen slot holds anything, said plainly: a
                 * Load that silently does nothing is the worst outcome. */
                if (loaded_game_index >= 0) {
                    SDL_PathInfo info;
                    const std::string sp = state_path(state_slot);
                    if (SDL_GetPathInfo(sp.c_str(), &info) &&
                        info.type == SDL_PATHTYPE_FILE) {
                        SDL_Time t = (SDL_Time)info.modify_time;
                        SDL_DateTime dt{};
                        if (SDL_TimeToDateTime(t, &dt, true))
                            TextDim("slot %d: %04d-%02d-%02d %02d:%02d",
                                    state_slot + 1, dt.year, dt.month, dt.day,
                                    dt.hour, dt.minute);
                        else
                            TextDim("slot %d: saved", state_slot + 1);
                    } else {
                        TextDim("slot %d: empty", state_slot + 1);
                    }
                }
                const ImVec2 hw(ImGui::GetFontSize() * 5.8f, 0);
                if (ImGui::Button("Save", hw)) do_save_state(state_slot);
                ImGui::SameLine();
                if (ImGui::Button("Load", hw)) {
                    do_load_state(state_slot);
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
                if (ImGui::Button("Back to the shelf", bw)) {
                    /* Written here too, not only at exit: an app that is
                     * force-quit should not forget the clock -- or, far
                     * worse, an afternoon of somebody's progress. */
                    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
                    save_bram();
                    pointer.seen = false;   /* no sight waiting on the shelf */
                    running_view = false;
                    show_pause = false;
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

        /* ---- whatever RetroMedia has finished ---- */
        {
            saturn::MediaResult mr;
            while (saturn::media_poll(mr)) {
                media_busy = false;
                if (!mr.message.empty()) media_message = mr.message;
                switch (mr.op) {
                case saturn::MediaOp::Status:
                case saturn::MediaOp::Login:
                case saturn::MediaOp::Logout:
                    account = mr.account;
                    /* The password is not kept a moment longer than the
                     * request that used it. */
                    SDL_memset(media_pass, 0, sizeof media_pass);
                    /* Cover art is free and unmetered for any signed-in
                     * account; only downloading needs an administrator. So the
                     * catalogue is fetched for anybody who signs in. */
                    if (account.signed_in)
                        saturn::media_begin_catalogue("", "", account.is_admin);
                    break;
                case saturn::MediaOp::Catalogue:
                    if (mr.ok) {
                        catalogue = mr.games;
                        /* The first, unfiltered answer is the one worth
                         * keeping for the shelf. */
                        if (!have_full_catalogue) {
                            have_full_catalogue = true;
                            for (const saturn::MediaGame &g : mr.games)
                                by_key[match_key(g.title)] = g;
                        }
                    }
                    break;
                case saturn::MediaOp::Artwork: {
                    if (art_in_flight > 0) art_in_flight--;
                    if (!mr.ok) break;
                    /* The reply names the game; which picture it was comes
                     * back in the path we asked with. */
                    int w = 0, h = 0;
                    std::vector<unsigned char> rgba;
                    if (!saturn::media_read_art(mr.art.path, w, h, rgba)) break;
                    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                                                       SDL_TEXTUREACCESS_STATIC, w, h);
                    if (!t) break;
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
                    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
                    Art &a = art[art_pending.count(mr.art.slug)
                                 ? art_pending[mr.art.slug] : mr.art.slug];
                    if (a.tex) SDL_DestroyTexture(a.tex);
                    a.tex = t; a.w = w; a.h = h;
                    break;
                }
                case saturn::MediaOp::Download:
                    if (mr.ok) rescan();      /* it is a disc on the shelf now */
                    break;
                default: break;
                }
            }
            downloads_offered = saturn::media_available() &&
                                saturn::media_downloads_available() &&
                                account.signed_in && account.is_admin;
            if (!downloads_offered && face == Face::Downloads) face = Face::Discs;
        }

        /* One place decides whether the Saturn is running, and it is the only
         * caller of the pause. Scattering set_presentation_paused around the
         * places that change the view is how you end up with a machine still
         * playing music behind a menu. */
        {
            const bool want = running_view && !show_pause;
            if (want != machine_running) {
                machine_running = want;
                ymir_bridge_set_presentation_paused(ymir, want ? 0 : 1);
            }
        }

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

    save_bram();
    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    saturn::save_app_config(cfg_path, cfg);
    /* The clock and the language, so the BIOS does not ask again. */
    ymir_bridge_save_smpc_state(ymir, smpc_path.c_str());
    if (frame) SDL_DestroyTexture(frame);
    if (logo) SDL_DestroyTexture(logo);
    for (auto &kv : art) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    if (art_console) SDL_DestroyTexture(art_console);
    if (art_pad) SDL_DestroyTexture(art_pad);
    if (art_gun) SDL_DestroyTexture(art_gun);
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
