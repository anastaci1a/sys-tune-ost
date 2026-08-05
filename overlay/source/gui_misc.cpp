#include "gui_misc.hpp"

#include "elm_overlayframe.hpp"
#include "elm_volume.hpp"
#include "applet_bgm.hpp"
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

std::string StateText(u64 state) {
    if (state == applet_bgm::SilentTitleId) {
        return "Silence";
    }
    if (state == applet_bgm::QlaunchTitleId) {
        return "Home";
    }
    if (state == applet_bgm::StartupTitleId) {
        return "Startup";
    }
    for (const auto& target : applet_bgm::Targets) {
        if (target.title_id == state) {
            return target.name;
        }
    }
    return "Unknown";
}

std::string TimingText(u64 begin_ms, u64 end_ms) {
    if (begin_ms == 0 || end_ms == 0 || end_ms < begin_ms) {
        return "Pending";
    }
    return std::to_string(end_ms - begin_ms) + " ms";
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

    list->addItem(new tsl::elm::CategoryHeader("Last Home resume diagnostics"));
    TuneTransitionDiagnostics diagnostics{};
    if (R_FAILED(tuneGetTransitionDiagnostics(&diagnostics)) ||
        diagnostics.sequence == 0) {
        auto item = new tsl::elm::ListItem(
            "No Home transition captured", "Return from an applet first");
        item->setValue("Return from an applet first", true);
        list->addItem(item);
    } else {
        list->addItem(new tsl::elm::ListItem(
            "Requested from", StateText(diagnostics.from_state)));
        list->addItem(new tsl::elm::ListItem(
            "Fade + activation",
            TimingText(diagnostics.requested_ms, diagnostics.active_ms)));
        list->addItem(new tsl::elm::ListItem(
            "Player scheduling",
            TimingText(diagnostics.active_ms, diagnostics.player_start_ms)));
        list->addItem(new tsl::elm::ListItem(
            "Source preparation",
            TimingText(diagnostics.player_start_ms,
                       diagnostics.source_ready_ms)));
        list->addItem(new tsl::elm::ListItem(
            "First audio buffer",
            TimingText(diagnostics.source_ready_ms,
                       diagnostics.first_buffer_ms)));
        list->addItem(new tsl::elm::ListItem(
            "Total after request",
            TimingText(diagnostics.requested_ms,
                       diagnostics.first_buffer_ms)));

        std::string cache_text = "Pending";
        if (diagnostics.cache_attempted) {
            cache_text = diagnostics.cache_hit ? "Retained" : "Reopened";
        }
        list->addItem(new tsl::elm::ListItem("Home decoder", cache_text));
    }

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 or \uE0E0 Change");
    frame->setContent(list);
    return frame;
}
