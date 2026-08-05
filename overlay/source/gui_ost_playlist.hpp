#pragma once

#include "elm_overlayframe.hpp"

#include <tesla.hpp>
#include <string>
#include <vector>

class OstPlaylistGui final : public tsl::Gui {
  private:
    u64 m_title_id{};
    std::string m_name;
    bool m_startup{};
    SysTuneOverlayFrame* m_frame{};
    tsl::elm::List* m_list{};
    tsl::elm::CategoryHeader* m_playlist_header{};
    tsl::elm::ListItem* m_empty_item{};
    std::vector<std::string> m_paths;
    std::vector<tsl::elm::ListItem*> m_track_items;
    u32 m_known_count{};
    u32 m_seen_revision{};

  public:
    OstPlaylistGui(u64 title_id, std::string name, bool startup);
    tsl::elm::Element* createUI() final;
    void update() final;
    bool handleInput(u64 keysDown, u64 keysHeld,
                     const HidTouchState& touchPos,
                     HidAnalogStickState leftJoyStick,
                     HidAnalogStickState rightJoyStick) final;

  private:
    void populate();
    std::vector<std::string> readPlaylist() const;
    void syncPlaylist();
    void addTrackItem(const std::string& path);
    void removeTrack(u32 index);
    void moveTrack(u32 index, u32 destination);
    void clearPlaylist();
    void updatePlaylistSummary();
    void markPlaylistChanged();
};
