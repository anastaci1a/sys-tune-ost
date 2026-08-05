#include "gui_misc.hpp"

#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"
#include "elm_volume.hpp"
#include "gui_qlaunch_scene_diagnostics.hpp"
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

    list->addItem(new tsl::elm::CategoryHeader("Track Boundaries"));
    list->addItem(new ElmTextBlock(
        "Used at the beginning and natural end of a track.\n"
        "Tracks do not overlap."));
    list->addItem(MakeDurationSlider(
        "Fade In", config::get_fade_in_ms(), config::set_fade_in_ms));
    list->addItem(MakeDurationSlider(
        "Fade Out", config::get_fade_out_ms(), config::set_fade_out_ms));

    list->addItem(new tsl::elm::CategoryHeader("Mid-Song Transitions"));
    list->addItem(new ElmTextBlock(
        "Used when playback is interrupted or resumed,\n"
        "including Home Menu pause and resume."));
    list->addItem(MakeDurationSlider(
        "Mid-Song Fade In", config::get_mid_song_fade_in_ms(),
        config::set_mid_song_fade_in_ms));
    list->addItem(MakeDurationSlider(
        "Mid-Song Fade Out", config::get_mid_song_fade_out_ms(),
        config::set_mid_song_fade_out_ms));

    list->addItem(new tsl::elm::CategoryHeader("Experimental"));
    list->addItem(new ElmTextBlock(
        "Inspect raw qlaunch scene changes.\n"
        "This diagnostic does not alter playlists."));
    auto diagnostics = new tsl::elm::ListItem("Qlaunch Scene Diagnostics");
    diagnostics->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<QlaunchSceneDiagnosticsGui>();
            return true;
        }
        return false;
    });
    list->addItem(diagnostics);

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 Adjust");
    frame->setContent(list);
    return frame;
}
