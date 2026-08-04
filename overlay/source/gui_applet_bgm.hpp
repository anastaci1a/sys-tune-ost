#pragma once

#include "elm_overlayframe.hpp"

#include <tesla.hpp>
#include <utility>
#include <vector>

class AppletBgmGui final : public tsl::Gui {
  private:
    SysTuneOverlayFrame* m_frame{};
    tsl::elm::List* m_list{};
    std::vector<std::pair<u64, tsl::elm::ListItem*>> m_track_items{};

  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void addTarget(u64 title_id, const char* name);
    void updateTargetValue(u64 title_id, tsl::elm::ListItem* item);
};
