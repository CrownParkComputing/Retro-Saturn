#include "saturn_artwork.h"
#include "saturn_config.h"
#include "saturn_setup.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

void put(const fs::path &path) { std::ofstream(path) << "fixture"; }

int main()
{
    const fs::path root = fs::temp_directory_path() /
        ("retro-saturn-library-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root / "cd");
        fs::create_directories(root / "GAMES");
        fs::create_directories(root / "Artwork");
        fs::create_directories(root / "Saves");
        put(root / "GAMES/Daytona USA (USA).chd");
        put(root / "Artwork/DAYTONA USA (USA).JpG");
        put(root / "Artwork/Panzer Dragoon Saga (USA).png");
        put(root / "Artwork/Daytona USA (Japan).png");
        put(root / "Artwork/Unrelated.txt");

        saturn::AppConfig config;
        saturn::configure_parent_folder(config, root.string());
        check(config.disc_root == (root / "GAMES").string(), "populated games folder must beat empty cd folder");
        check(config.artwork_dir == (root / "Artwork").string(), "reuse existing mixed-case artwork folder");
        check(config.saves_dir == (root / "Saves").string(), "preserve existing Saves folder");
        check(saturn::parent_for_discs(config.disc_root + "/") == root.string(), "infer parent of existing game folder");
        check(!fs::exists(root / "artwork"), "must not create a duplicate artwork folder");

        auto games = saturn::scan_discs(config.disc_root);
        check(games.size() == 1, "scan games from selected parent");
        saturn::ArtworkLibrary art;
        art.scan(config.artwork_dir);
        check(art.path_for(games.front(), {}) == (root / "Artwork/DAYTONA USA (USA).JpG").string(), "match image name and extension case-insensitively");
        games.front().discs.front().file = "Daytona USA (Europe).chd";
        games.front().title = "Daytona USA (Europe)";
        check(art.path_for(games.front(), {}).empty(), "do not match artwork for a different regional filename");
        saturn::Game multi;
        multi.title = "Panzer Dragoon Saga (USA)";
        saturn::Disc disc;
        disc.file = "Panzer Dragoon Saga (USA) (Disc 1).chd";
        multi.discs.push_back(disc);
        check(art.path_for(multi, {}) == (root / "Artwork/Panzer Dragoon Saga (USA).png").string(), "match shared multi-disc title");
        fs::remove(root / "Artwork/Panzer Dragoon Saga (USA).png");
        art.scan(config.artwork_dir);
        check(art.path_for(multi, {}).empty(), "rescan removes stale artwork");

        const fs::path flat = root / "Flat";
        fs::create_directory(flat);
        put(flat / "Virtua Fighter.chd");
        saturn::configure_parent_folder(config, flat.string());
        check(config.disc_root == flat.string(), "preserve a flat legacy library");
        check(fs::is_directory(flat / "artwork"), "create artwork inside the selected parent");
        check(fs::exists(flat / "Virtua Fighter.chd"), "leave existing discs in place");
        check(saturn::save_app_config((root / "config").string(), config), "save folder settings");
        saturn::AppConfig restored;
        check(saturn::load_app_config((root / "config").string(), restored), "reload folder settings");
        check(restored.parent_root == config.parent_root && restored.artwork_dir == config.artwork_dir,
              "remember parent and artwork folders");
        fs::remove_all(root);
        std::cout << "OK: parent folders, local artwork matching and persistence\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
