#pragma once

#include <tesla.hpp>

#include <string>
#include <vector>

class ElmTextBlock final : public tsl::elm::Element {
public:
    explicit ElmTextBlock(const std::string& text) {
        size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            m_lines.emplace_back(text.substr(start, end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        m_height = 18 + m_lines.size() * 21;
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        s32 baseline = getY() + 24;
        for (const auto& line : m_lines) {
            renderer->drawString(
                line.c_str(), false, getX() + 13, baseline, 17,
                a(tsl::style::color::ColorDescription), getWidth() - 26);
            baseline += 21;
        }
    }

    void layout(u16, u16, u16, u16) override {
        setBoundaries(getX(), getY(), getWidth(), m_height);
    }

    tsl::elm::Element* requestFocus(
        tsl::elm::Element*, tsl::FocusDirection) override {
        return nullptr;
    }

private:
    std::vector<std::string> m_lines;
    u16 m_height{};
};
