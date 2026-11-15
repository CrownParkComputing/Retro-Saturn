#ifndef SATURN_APPLE_H
#define SATURN_APPLE_H

#include <string>

namespace saturn {
using ApplePickResult = void (*)(const std::string &);
void apple_restore_folder_access();
std::string apple_resolve_path(const std::string &path);
void apple_pick_document(bool folder, const std::string &initial, ApplePickResult result);
}

#endif
