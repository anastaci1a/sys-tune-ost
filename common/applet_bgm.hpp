#pragma once

#include <array>
#include <switch.h>

namespace applet_bgm {

struct Target {
    u64 title_id;
    const char* name;
};

constexpr u64 QlaunchTitleId = 0x0100000000001000ULL;

// User-facing order for the overlay.
constexpr std::array Targets = {
    Target{0x010000000000100DULL, "Album / Photos"},
    Target{0x0100000000001003ULL, "Controllers"},
    Target{0x0100000000001013ULL, "User Page"},
    Target{0x0100000000001009ULL, "Mii Editor"},
    Target{0x0100000000001002ULL, "Amiibo"},
    Target{0x010000000000100BULL, "Nintendo eShop"},
    Target{0x0100000000001007ULL, "User Select"},
    Target{0x0100000000001006ULL, "Network Connection"},
    Target{0x010000000000100AULL, "Web Applet"},
    Target{0x010000000000100FULL, "Offline Manual"},
    Target{0x0100000000001010ULL, "Share / Login"},
    Target{0x0100000000001011ULL, "Wi-Fi Login"},
    Target{0x0100000000001008ULL, "Software Keyboard"},
    Target{0x0100000000001001ULL, "Authentication"},
    Target{0x0100000000001004ULL, "Data Management"},
    Target{0x0100000000001005ULL, "Error Dialog"},
};

// Some applets can open another applet (for example, eShop can open the
// software keyboard), so nested/specific processes are detected first.
constexpr std::array DetectionTitleIds = {
    0x0100000000001005ULL,
    0x0100000000001008ULL,
    0x010000000000100DULL,
    0x0100000000001003ULL,
    0x0100000000001013ULL,
    0x0100000000001009ULL,
    0x0100000000001002ULL,
    0x010000000000100BULL,
    0x0100000000001007ULL,
    0x0100000000001006ULL,
    0x010000000000100AULL,
    0x010000000000100FULL,
    0x0100000000001010ULL,
    0x0100000000001011ULL,
    0x0100000000001001ULL,
    0x0100000000001004ULL,
};

// qlaunch implements both the Home Menu and retail System Settings. There is
// no separate Settings process to distinguish without a firmware-specific hook.
constexpr Target QlaunchTarget{QlaunchTitleId, "Home Menu / Settings"};

}
