#include "gui_main.hpp"

#include "elm_overlayframe.hpp"
#include "gui_applet_bgm.hpp"
#include "gui_misc.hpp"
#include "config/config.hpp"

MainGui::MainGui() {
    m_status_bar    = new StatusBar();
}

tsl::elm::Element *MainGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list  = new tsl::elm::List();

    /* Current track. */
    list->addItem(this->m_status_bar, tsl::style::ListItemDefaultHeight * 3);

    list->addItem(new tsl::elm::CategoryHeader("System UI Soundtracks"));

    auto enabled = new tsl::elm::ToggleListItem(
        "System UI OST", config::get_applet_bgm_enabled(), "On", "Off");
    enabled->setStateChangedListener([](bool value) {
        config::set_applet_bgm_enabled(value);
        tuneReloadAppletBgm();
    });
    list->addItem(enabled);

    auto soundtracks_button = new tsl::elm::ListItem("Manage Soundtracks");
    soundtracks_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<AppletBgmGui>();
            return true;
        }
        return false;
    });
    list->addItem(soundtracks_button);

    auto misc_button = new tsl::elm::ListItem("Misc Options");
    misc_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<MiscGui>();
            return true;
        }
        return false;
    });
    list->addItem(misc_button);

    frame->setContent(list);

    return frame;
}

void MainGui::update() {
    static u8 tick = 0;
    /* Update status 4 times per second. */
    if ((tick % 15) == 0)
        this->m_status_bar->update();
    tick++;
}
