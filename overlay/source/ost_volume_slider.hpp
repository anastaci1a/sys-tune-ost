#pragma once

#include "applet_bgm.hpp"
#include "elm_volume.hpp"

#include <algorithm>
#include <string>

namespace ost_volume_ui {

constexpr size_t StepCount = 31;
constexpr float StepsPerUnit = 20.f;

inline u8 ToStep(float volume) {
    const auto clamped = std::clamp(volume, 0.f, applet_bgm::VolumeMax);
    return static_cast<u8>(clamped * StepsPerUnit + 0.5f);
}

inline std::string SliderText(const std::string& label, u8 step) {
    return label + ": " + std::to_string(static_cast<u32>(step) * 5) + "%";
}

template<typename Listener>
ElmVolume* MakeVolumeSlider(
    const char* label, float initial, Listener listener) {
    const std::string name{label};
    const auto initial_step = ToStep(initial);
    auto* slider = new ElmVolume(
        "\uE13C", SliderText(name, initial_step), StepCount);
    slider->setProgress(initial_step);
    slider->setValueChangedListener(
        [slider, name, listener](u8 step) {
            const auto volume = static_cast<float>(step) / StepsPerUnit;
            listener(volume);
            slider->setName(SliderText(name, step));
        });
    return slider;
}

}
