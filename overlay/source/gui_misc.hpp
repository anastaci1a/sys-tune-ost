#pragma once

#include <tesla.hpp>

class MiscGui final : public tsl::Gui {
  public:
    tsl::elm::Element* createUI() final;
};
