#pragma once

#include <tesla.hpp>

class LoadingSettingsGui final : public tsl::Gui {
  public:
    tsl::elm::Element* createUI() final;
};
