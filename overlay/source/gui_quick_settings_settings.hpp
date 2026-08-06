#pragma once

#include <tesla.hpp>

class QuickSettingsSettingsGui final : public tsl::Gui {
  public:
    tsl::elm::Element* createUI() final;
};
