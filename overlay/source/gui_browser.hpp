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
    u64 m_ost_state{};
    std::string m_ost_name{};

  public:
    BrowserGui(u64 ost_state, std::string ost_name);
    ~BrowserGui();

    tsl::elm::Element *createUI() override;
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override;

  private:
    void scanCwd();
    void upCwd();
    void addAllToPlaylist();
};
