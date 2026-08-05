#pragma once

#include "elm_overlayframe.hpp"

#include <tesla.hpp>
#include <vector>

class AppletBgmGui final : public tsl::Gui {
  private:
    struct PlaylistItem {
        u64 title_id{};
        tsl::elm::ListItem* item{};
        u32 revision{};
    };

    tsl::elm::List* m_list{};
    std::vector<PlaylistItem> m_playlist_items{};
    u32 m_global_revision{};

  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void addTarget(u64 title_id, const char* name, bool startup = false);
    void updateTargetValue(PlaylistItem& playlist_item);
};
