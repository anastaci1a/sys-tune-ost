#include "gui_misc.hpp"

#include "elm_overlayframe.hpp"
#include "elm_volume.hpp"
#include "config/config.hpp"
#include "tune.h"

#include <algorithm>
#include <array>

namespace {

constexpr std::array<u32, 26> FadeDurations = {
    0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500,
    600, 700, 800, 900, 1000,
    1250, 1500, 1750, 2000,
    2500, 3000, 3500, 4000, 4500, 5000,
};

std::string DurationText(u32 milliseconds) {
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

size_t NearestFadeStep(u32 value) {
    size_t best = 0;
    auto bestDistance = value;
    for (size_t i = 1; i < FadeDurations.size(); i++) {
        const auto distance = value > FadeDurations[i]
            ? value - FadeDurations[i] : FadeDurations[i] - value;
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

ElmVolume* MakeDurationSlider(
    const char* label, u32 initial, void (*setter)(u32)) {
    const auto value = std::min(initial, FadeDurations.back());
    const auto step = NearestFadeStep(value);
    const std::string name = label;
    auto slider = new ElmVolume(
        "\uE13C", name + ": " + DurationText(value), FadeDurations.size());
    slider->setProgress(step);
    slider->setValueChangedListener([slider, name, setter](u8 value) {
        const auto milliseconds = FadeDurations[value];
        setter(milliseconds);
        tuneReloadOstMisc();
        slider->setName(name + ": " + DurationText(milliseconds));
    });
    return slider;
}

}

tsl::elm::Element* MiscGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Transitions"));
    list->addItem(new tsl::elm::CategoryHeader(
        "Fade-out then fade-in; tracks do not overlap", true));
    list->addItem(new tsl::elm::CategoryHeader(
        "Short fades use finer slider steps", true));
    list->addItem(MakeDurationSlider(
        "Fade in", config::get_fade_in_ms(), config::set_fade_in_ms));
    list->addItem(MakeDurationSlider(
        "Fade out", config::get_fade_out_ms(), config::set_fade_out_ms));

    list->addItem(new tsl::elm::CategoryHeader("Output"));
    constexpr size_t VolumeSteps = 21;
    float volume = 1.f;
    tuneGetVolume(&volume);
    auto volume_slider = new ElmVolume("\uE13C", "OST Volume", VolumeSteps);
    volume_slider->setProgress(std::clamp<u8>(
        static_cast<u8>(volume * (VolumeSteps - 1)), 0, VolumeSteps - 1));
    volume_slider->setValueChangedListener([](u8 value) {
        tuneSetVolume(static_cast<float>(value) / static_cast<float>(VolumeSteps - 1));
    });
    list->addItem(volume_slider);

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 Adjust");
    frame->setContent(list);
    return frame;
}
