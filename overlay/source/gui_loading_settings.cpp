#include "gui_loading_settings.hpp"

#include "config/config.hpp"
#include "duration_slider.hpp"
#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"

tsl::elm::Element* LoadingSettingsGui::createUI() {
    auto* frame = new SysTuneOverlayFrame();
    auto* list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Loading Screen Timing"));
    list->addItem(new ElmTextBlock(
        "Start waits before music begins.\n"
        "End keeps it playing after launch ends."));
    list->addItem(duration_ui::MakeDurationSlider(
        "Start Delay", config::get_loading_start_delay_ms(),
        config::set_loading_start_delay_ms));
    list->addItem(duration_ui::MakeDurationSlider(
        "End Delay", config::get_loading_end_delay_ms(),
        config::set_loading_end_delay_ms));

    list->addItem(new ElmTextBlock(
        "End Delay uses the focus signal.\n"
        "Other UI playlists still take priority."));

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 Adjust");
    frame->setContent(list);
    return frame;
}
