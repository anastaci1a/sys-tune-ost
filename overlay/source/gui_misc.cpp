#include "gui_misc.hpp"

#include "elm_overlayframe.hpp"
#include "elm_volume.hpp"
#include "config/config.hpp"
#include "tune.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr std::array<u32, 9> FadeDurations = {
    0, 250, 500, 750, 1000, 1500, 2000, 3000, 5000,
};

std::string DurationText(u32 milliseconds) {
    if (milliseconds == 0) {
        return "Off";
    }
    if (milliseconds < 1000) {
        return std::to_string(milliseconds) + " ms";
    }
    const auto whole = milliseconds / 1000;
    const auto tenth = (milliseconds % 1000) / 100;
    return tenth == 0
        ? std::to_string(whole) + " s"
        : std::to_string(whole) + "." + std::to_string(tenth) + " s";
}

size_t NearestDuration(u32 value) {
    size_t best = 0;
    u32 best_distance = UINT32_MAX;
    for (size_t i = 0; i < FadeDurations.size(); i++) {
        const auto distance = value > FadeDurations[i]
            ? value - FadeDurations[i] : FadeDurations[i] - value;
        if (distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

tsl::elm::ListItem* MakeDurationItem(
    const char* label, u32 initial, void (*setter)(u32)) {
    auto index = NearestDuration(initial);
    auto item = new tsl::elm::ListItem(label, DurationText(initial));
    item->setClickListener([item, index, setter](u64 keys) mutable {
        if (keys & HidNpadButton_Left) {
            index = index == 0 ? FadeDurations.size() - 1 : index - 1;
        } else if (keys & (HidNpadButton_A | HidNpadButton_Right)) {
            index = (index + 1) % FadeDurations.size();
        } else {
            return false;
        }

        setter(FadeDurations[index]);
        tuneReloadOstMisc();
        item->setValue(DurationText(FadeDurations[index]), FadeDurations[index] == 0);
        return true;
    });
    return item;
}

}

tsl::elm::Element* MiscGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Transitions"));
    list->addItem(new tsl::elm::CategoryHeader(
        "Fade-out then fade-in; tracks do not overlap", true));
    list->addItem(MakeDurationItem(
        "Fade in", config::get_fade_in_ms(), config::set_fade_in_ms));
    list->addItem(MakeDurationItem(
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

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 or \uE0E0 Change");
    frame->setContent(list);
    return frame;
}
