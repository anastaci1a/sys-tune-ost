#pragma once

#include <tesla.hpp>

#include "elm_overlayframe.hpp"

class BrowserGui final : public tsl::Gui {
  private:
    SysTuneOverlayFrame* m_frame;
    tsl::elm::List *m_list;
    FsFileSystem m_fs;
    bool has_music;
    char cwd[FS_MAX_PATH];
    u64 m_applet_bgm_tid{};
    std::string m_applet_bgm_name{};

  public:
    BrowserGui();
    BrowserGui(u64 applet_bgm_tid, std::string applet_bgm_name);
    ~BrowserGui();

    tsl::elm::Element *createUI() override;
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override;

  private:
    void scanCwd();
    void upCwd();
    void addAllToPlaylist();
    void infoAlert(const std::string &title, const std::string &text);
};
