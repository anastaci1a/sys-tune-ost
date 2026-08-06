#pragma once

#include "elm_volume.hpp"
#include "tune.h"

#include <algorithm>
#include <array>
#include <string>

namespace duration_ui {

inline constexpr std::array<u32, 26> StandardDurations = {
    0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500,
    600, 700, 800, 900, 1000,
    1250, 1500, 1750, 2000,
    2500, 3000, 3500, 4000, 4500, 5000,
};

inline constexpr std::array<u32, 16> QuickSettingsHoldDurations = {
    100, 150, 200, 250, 300, 350, 400, 450,
    500, 600, 700, 800, 900, 1000, 1250, 1500,
};

inline std::string DurationText(u32 milliseconds) {
    if (milliseconds == 0) {
        return "Off";
    }
    if (milliseconds < 1000) {
        return std::to_string(milliseconds) + " ms";
    }
    const auto whole = milliseconds / 1000;
    auto fraction = std::to_string(1000 + milliseconds % 1000).substr(1);
    while (!fraction.empty() && fraction.back() == '0') {
        fraction.pop_back();
    }
    return std::to_string(whole) +
        (fraction.empty() ? "" : "." + fraction) + " s";
}

template<size_t Count>
size_t NearestStep(u32 value, const std::array<u32, Count>& durations) {
    size_t best = 0;
    auto best_distance = value > durations[0]
        ? value - durations[0] : durations[0] - value;
    for (size_t index = 1; index < durations.size(); ++index) {
        const auto distance = value > durations[index]
            ? value - durations[index] : durations[index] - value;
        if (distance < best_distance) {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

template<size_t Count, typename Listener>
ElmVolume* MakeDurationSlider(
    const char* label, u32 initial,
    const std::array<u32, Count>& durations, Listener listener) {
    static_assert(Count > 1 && Count <= 255);
    const auto step = NearestStep(initial, durations);
    const std::string name{label};
    auto* slider = new ElmVolume(
        "\uE13C", name + ": " + DurationText(durations[step]), Count);
    slider->setProgress(step);
    slider->setValueChangedListener(
        [slider, name, durations, listener](u8 value) {
            const auto milliseconds = durations[value];
            listener(milliseconds);
            tuneReloadOstMisc();
            slider->setName(name + ": " + DurationText(milliseconds));
        });
    return slider;
}

template<typename Listener>
ElmVolume* MakeDurationSlider(
    const char* label, u32 initial, Listener listener) {
    return MakeDurationSlider(label, initial, StandardDurations, listener);
}

}
