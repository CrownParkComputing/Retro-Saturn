#include "saturn_config.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace saturn {

const char *peripheral_name(Peripheral p)
{
    switch (p) {
    case Peripheral::None:         return "Empty";
    case Peripheral::ControlPad:   return "Control Pad";
    case Peripheral::AnalogPad:    return "3D Control Pad";
    case Peripheral::ArcadeRacer:  return "Arcade Racer";
    case Peripheral::MissionStick: return "Mission Stick";
    case Peripheral::VirtuaGun:    return "Virtua Gun";
    case Peripheral::ShuttleMouse: return "Shuttle Mouse";
    }
    return "Empty";
}

namespace {

std::string read_file(const std::string &path)
{
    SDL_IOStream *in = SDL_IOFromFile(path.c_str(), "rb");
    if (!in) return std::string();
    const Sint64 size = SDL_GetIOSize(in);
    if (size <= 0 || size > (1 << 20)) { SDL_CloseIO(in); return std::string(); }
    std::string text((size_t)size, '\0');
    const size_t got = SDL_ReadIO(in, text.data(), text.size());
    SDL_CloseIO(in);
    if (got != text.size()) return std::string();
    return text;
}

bool write_file(const std::string &path, const std::string &text)
{
    SDL_IOStream *out = SDL_IOFromFile(path.c_str(), "wb");
    if (!out) return false;
    const bool ok = SDL_WriteIO(out, text.data(), text.size()) == text.size();
    SDL_CloseIO(out);
    return ok;
}

std::map<std::string, std::string> parse_kv(const std::string &text)
{
    std::map<std::string, std::string> kv;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string v = line.substr(eq + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();
        kv[line.substr(0, eq)] = v;
    }
    return kv;
}

std::string str_of(const std::map<std::string, std::string> &kv, const char *k,
                   const std::string &dflt)
{
    auto it = kv.find(k);
    return it == kv.end() ? dflt : it->second;
}
int int_of(const std::map<std::string, std::string> &kv, const char *k, int dflt)
{
    auto it = kv.find(k);
    return it == kv.end() ? dflt : std::atoi(it->second.c_str());
}
bool bool_of(const std::map<std::string, std::string> &kv, const char *k, bool dflt)
{
    auto it = kv.find(k);
    return it == kv.end() ? dflt : (it->second == "1");
}

/* Clamped on the way in, not trusted. A hand-edited file should make the app
 * behave oddly at worst, never index off the end of an enum. */
Peripheral peripheral_of(const std::map<std::string, std::string> &kv,
                         const char *k, Peripheral dflt)
{
    const int v = int_of(kv, k, (int)dflt);
    return (v >= 0 && v <= (int)Peripheral::ShuttleMouse) ? (Peripheral)v : dflt;
}

} /* namespace */

bool load_app_config(const std::string &path, AppConfig &out)
{
    const std::string text = read_file(path);
    if (text.empty()) return false;
    const auto kv = parse_kv(text);

    out.disc_root   = str_of(kv, "disc_root", out.disc_root);
    out.bios_path   = str_of(kv, "bios_path", out.bios_path);
    out.wizard_done = bool_of(kv, "wizard_done", false);

    Settings &s = out.machine;
    s.region_auto          = bool_of(kv, "region_auto", s.region_auto);
    s.video_standard       = int_of(kv, "video_standard", s.video_standard) ? 1 : 0;
    s.sh2_cache            = bool_of(kv, "sh2_cache", s.sh2_cache);
    s.sh2_clock            = int_of(kv, "sh2_clock", s.sh2_clock);
    if (s.sh2_clock < 25 || s.sh2_clock > 400) s.sh2_clock = 100;
    s.threaded_vdp1        = bool_of(kv, "threaded_vdp1", s.threaded_vdp1);
    s.threaded_vdp2        = bool_of(kv, "threaded_vdp2", s.threaded_vdp2);
    s.threaded_deinterlace = bool_of(kv, "threaded_deinterlace", s.threaded_deinterlace);
    s.audio_interpolation  = int_of(kv, "audio_interpolation", s.audio_interpolation) ? 1 : 0;
    s.cd_read_speed        = int_of(kv, "cd_read_speed", s.cd_read_speed);
    if (s.cd_read_speed < 2 || s.cd_read_speed > 200) s.cd_read_speed = 2;
    s.cdblock_lle          = bool_of(kv, "cdblock_lle", s.cdblock_lle);
    s.port1                = peripheral_of(kv, "port1", s.port1);
    s.port2                = peripheral_of(kv, "port2", s.port2);
    return true;
}

bool save_app_config(const std::string &path, const AppConfig &cfg)
{
    const Settings &s = cfg.machine;
    std::string t = "# Retro-Saturn settings\n";
    t += "disc_root=" + cfg.disc_root + "\n";
    t += "bios_path=" + cfg.bios_path + "\n";
    t += "wizard_done="; t += cfg.wizard_done ? "1" : "0"; t += "\n";
    t += "region_auto="; t += s.region_auto ? "1" : "0"; t += "\n";
    t += "video_standard=" + std::to_string(s.video_standard) + "\n";
    t += "sh2_cache="; t += s.sh2_cache ? "1" : "0"; t += "\n";
    t += "sh2_clock=" + std::to_string(s.sh2_clock) + "\n";
    t += "threaded_vdp1="; t += s.threaded_vdp1 ? "1" : "0"; t += "\n";
    t += "threaded_vdp2="; t += s.threaded_vdp2 ? "1" : "0"; t += "\n";
    t += "threaded_deinterlace="; t += s.threaded_deinterlace ? "1" : "0"; t += "\n";
    t += "audio_interpolation=" + std::to_string(s.audio_interpolation) + "\n";
    t += "cd_read_speed=" + std::to_string(s.cd_read_speed) + "\n";
    t += "cdblock_lle="; t += s.cdblock_lle ? "1" : "0"; t += "\n";
    t += "port1=" + std::to_string((int)s.port1) + "\n";
    t += "port2=" + std::to_string((int)s.port2) + "\n";
    return write_file(path, t);
}

} /* namespace saturn */
