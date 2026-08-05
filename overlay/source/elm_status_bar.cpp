#include "elm_status_bar.hpp"

#include "applet_bgm.hpp"
#include "symbol.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

    char path_buffer[FS_MAX_PATH] = "";
    char current_buffer[0x20] = "";
    char total_buffer[0x20] = "";

    void NullLastDot(char *str) {
        const auto length = std::strlen(str);
        if (length == 0) {
            return;
        }
        char *end = str + length - 1;
        while (str != end) {
            if (*end == '.') {
                *end = '\0';
                return;
            }
            end--;
        }
    }

    std::string StateLabel(u64 title_id) {
        if (title_id == applet_bgm::StartupTitleId) {
            return "Startup Sound";
        }
        if (title_id == applet_bgm::WakeStartupTitleId) {
            return "Wake Startup Sound";
        }
        if (title_id == applet_bgm::QlaunchTitleId) {
            return "Home Menu OST";
        }
        if (title_id == applet_bgm::SettingsStateId) {
            return "System Settings OST";
        }
        if (title_id == applet_bgm::LockStateId) {
            return "Lock Screen OST";
        }
        for (const auto& target : applet_bgm::Targets) {
            if (target.title_id == title_id) {
                return std::string(target.name) + " OST";
            }
        }
        return "No Active UI OST";
    }

}

StatusBar::StatusBar() {
    update();
}

tsl::elm::Element *StatusBar::requestFocus(tsl::elm::Element *oldFocus, tsl::FocusDirection direction) {
    return this;
}

void StatusBar::draw(tsl::gfx::Renderer *renderer) {
    if (this->m_touched && Element::getInputMode() == tsl::InputMode::Touch) {
        renderer->drawRect(ELEMENT_BOUNDS(this), a(tsl::style::color::ColorClickAnimation));
    }

    if (this->m_text_width == 0) {
        /* Get base width. */
        auto [width, height] = renderer->drawString(this->m_current_track.data(), false, 0, 0, 26, tsl::style::color::ColorTransparent);
        this->m_truncated = static_cast<s32>(width) > (this->getWidth() - 30);
        if (this->m_truncated) {
            /* Get width with spacing. */
            this->m_scroll_text = std::string(m_current_track).append("       ");
            auto [ex_width, ex_height] = renderer->drawString(this->m_scroll_text.c_str(), false, 0, 0, 26, tsl::style::color::ColorTransparent);
            this->m_scroll_text.append(m_current_track);
            this->m_text_width = ex_width;
        } else {
            this->m_text_width = width;
        }
    }

    /* Current track. */
    if (this->m_truncated) {
        renderer->drawString(this->m_scroll_text.c_str(), false, this->getX() + 15 - this->m_scroll_offset, this->getY() + 40, 26, tsl::style::color::ColorText);
        if (this->m_counter == 120) {
            if (this->m_scroll_offset == this->m_text_width) {
                this->m_scroll_offset = 0;
                this->m_counter = 0;
            } else {
                this->m_scroll_offset++;
            }
        } else {
            this->m_counter++;
        }
    } else {
        renderer->drawString(this->m_current_track.data(), false, this->getX() + 15, this->getY() + 40, 26, tsl::style::color::ColorText);
    }

    /* Seek bar. */
    u32 bar_length = this->getWidth() - 30;
    renderer->drawRect(this->getX() + 15, this->getY() + tsl::style::ListItemDefaultHeight, bar_length, 3, 0xffff);

    if (this->m_percentage > 0) {
        renderer->drawCircle(this->getX() + 15, this->getY() + tsl::style::ListItemDefaultHeight + 1, 3, true, 0xf00f);
        renderer->drawRect(this->getX() + 15, this->getY() + tsl::style::ListItemDefaultHeight - 2, bar_length * this->m_percentage, 7, 0xf00f);
        renderer->drawCircle(this->getX() + 15 + bar_length * this->m_percentage, this->getY() + tsl::style::ListItemDefaultHeight + 1, 3, true, 0xf00f);
    }

    /* Progress */
    renderer->drawString(current_buffer, false, this->getX() + 15, this->getY() + CenterOfLine(1) + 8, 20, 0xffff);

    if (CanEditPlaybackPolicy()) {
        /* Repeat indicator */
        auto repeat_color = this->m_repeat ? tsl::style::color::ColorHighlight : tsl::style::color::ColorHeaderBar;
        if (this->m_repeat == TuneRepeatMode_One) {
            symbol::repeat::one::symbol.draw(GetRepeatX(), GetRepeatY(), renderer, repeat_color);
        } else {
            symbol::repeat::all::symbol.draw(GetRepeatX(), GetRepeatY(), renderer, repeat_color);
        }

        /* Shuffle indicator */
        auto shuffle_color = this->m_shuffle ? tsl::style::color::ColorHighlight : tsl::style::color::ColorHeaderBar;
        symbol::shuffle::symbol.draw(GetShuffleX(), GetShuffleY(), renderer, shuffle_color);
    }

    /* Song length */
    renderer->drawString(total_buffer, false, this->getX() + this->getWidth() - 75, this->getY() + CenterOfLine(1) + 8, 20, 0xffff);

    if (CanSeek()) {
        /* Backward button */
        symbol::backward::symbol.draw(GetBackwardX(), GetBackwardY(), renderer, tsl::style::color::ColorText);

        /* Forward button */
        symbol::forward::symbol.draw(GetForwardX(), GetForwardY(), renderer, tsl::style::color::ColorText);
    }

    if (CanNavigateQueue()) {
        /* Prev button */
        symbol::prev::symbol.draw(GetPrevX(), GetPrevY(), renderer, tsl::style::color::ColorText);

        /* Next button */
        symbol::next::symbol.draw(GetNextX(), GetNextY(), renderer, tsl::style::color::ColorText);
    }

    /* Current playback glyph */
    const auto label_width = renderer->drawString(
        this->m_state_label.c_str(), false, 0, 0, 16,
        tsl::style::color::ColorTransparent).first;
    renderer->drawString(
        this->m_state_label.c_str(), false,
        this->getX() + (this->getWidth() - label_width) / 2,
        this->getY() + CenterOfLine(2) - 32, 16,
        a(tsl::style::color::ColorDescription));

    if (HasPlayableSession()) {
        /* Current playback glyph */
        this->GetPlaybackSymbol().draw(GetPlayStateX(), GetPlayStateY(), renderer, tsl::style::color::ColorText);
    }
}

void StatusBar::layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) {
    this->setBoundaries(this->getX(), this->getY(), this->getWidth(), tsl::style::ListItemDefaultHeight * 3);
}

bool StatusBar::onClick(u64 keys) {
    u8 handled = 0;
    if (HasPlayableSession() && (keys & HidNpadButton_A)) {
        this->CyclePlay();
        handled++;
    }
    if (CanEditPlaybackPolicy() && (keys & HidNpadButton_X)) {
        this->CycleRepeat();
        handled++;
    }
    if (CanEditPlaybackPolicy() && (keys & HidNpadButton_Y)) {
        this->CycleShuffle();
        handled++;
    }
    if (CanNavigateQueue() && (keys & HidNpadButton_Right)) {
        this->Next();
        handled++;
    }
    if (CanNavigateQueue() && (keys & HidNpadButton_Left)) {
        this->Prev();
        handled++;
    }
    if (CanSeek() && (keys & HidNpadButton_ZL)) {
        this->Backward();
        handled++;
    }
    if (CanSeek() && (keys & HidNpadButton_ZR)) {
        this->Forward();
        handled++;
    }
    return handled;
}

#define TOUCHED(button) (currX > (Get##button##X() - 30) && currX < (Get##button##X() + 30) && prevY > (Get##button##Y() - 30) && prevY < (Get##button##Y() + 30))

bool StatusBar::onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY, s32 initialX, s32 initialY) {
    if (event == tsl::elm::TouchEvent::Touch)
        this->m_touched = this->inBounds(currX, currY);

    if (event == tsl::elm::TouchEvent::Release && this->m_touched) {
        this->m_touched = false;

        if (Element::getInputMode() == tsl::InputMode::Touch) {
            u16 handled = 0;
            if (CanEditPlaybackPolicy() && TOUCHED(Repeat)) {
                this->CycleRepeat();
                handled++;
            }
            if (CanEditPlaybackPolicy() && TOUCHED(Shuffle)) {
                this->CycleShuffle();
                handled++;
            }
            if (HasPlayableSession() && TOUCHED(PlayState)) {
                this->CyclePlay();
                handled++;
            }
            if (CanNavigateQueue() && TOUCHED(Prev)) {
                this->Prev();
                handled++;
            }
            if (CanNavigateQueue() && TOUCHED(Next)) {
                this->Next();
                handled++;
            }
            if (CanSeek() && TOUCHED(Forward)) {
                this->Forward();
                handled++;
            }
            if (CanSeek() && TOUCHED(Backward)) {
                this->Backward();
                handled++;
            }

            if (handled > 0) {
                this->m_clickAnimationProgress = 0;
                return true;
            }
        }
    }

    return false;
}

void StatusBar::update() {
    if (R_FAILED(tuneGetStatus(&this->m_playing)))
        this->m_playing = false;
    tuneGetRepeatMode(&this->m_repeat);
    tuneGetShuffleMode(&this->m_shuffle);
    tuneGetPlaylistSize(&this->m_playlist_size);
    if (R_SUCCEEDED(tuneGetActiveOstState(&this->m_active_state))) {
        this->m_state_label = StateLabel(this->m_active_state);
    }

    if (R_SUCCEEDED(tuneGetCurrentQueueItem(path_buffer, FS_MAX_PATH, &this->m_stats))) {
        /* Only show file name. Ignore path to file and extension. */
        NullLastDot(path_buffer);
        const auto slash = std::strrchr(path_buffer, '/');
        const auto name = slash ? slash + 1 : path_buffer;
        if (this->m_current_track != name) {
            this->m_current_track = name;
            this->m_text_width = 0;
            this->m_scroll_offset = 0;
            this->m_counter = 0;
        }
    } else {
        this->m_current_track = "Stopped!";
        this->m_stats = {};
        /* Reset scrolling text */
        this->m_text_width = 0;
        this->m_scroll_offset = 0;
        this->m_counter = 0;
    }
    /* Progress text and bar */
    u32 current = this->m_stats.sample_rate == 0
        ? 0 : this->m_stats.current_frame / this->m_stats.sample_rate;
    u32 total = this->m_stats.sample_rate == 0
        ? 0 : this->m_stats.total_frames / this->m_stats.sample_rate;
    this->m_percentage = this->m_stats.total_frames == 0
        ? 0.f
        : std::clamp(float(this->m_stats.current_frame) /
                     float(this->m_stats.total_frames), 0.0f, 1.0f);

    std::snprintf(current_buffer, sizeof(current_buffer), "%d:%02d", current / 60, current % 60);
    if (this->m_stats.total_frames == 0 && this->m_stats.sample_rate != 0) {
        std::snprintf(total_buffer, sizeof(total_buffer), "--:--");
    } else {
        std::snprintf(total_buffer, sizeof(total_buffer), "%d:%02d", total / 60, total % 60);
    }
}

void StatusBar::CycleRepeat() {
    if (!CanEditPlaybackPolicy()) {
        return;
    }
    this->m_repeat = static_cast<TuneRepeatMode>((this->m_repeat + 1) % TuneRepeatMode_Count);
    tuneSetRepeatMode(this->m_repeat);
}

void StatusBar::CycleShuffle() {
    if (!CanEditPlaybackPolicy()) {
        return;
    }
    this->m_shuffle = static_cast<TuneShuffleMode>((this->m_shuffle + 1) % TuneShuffleMode_Count);
    tuneSetShuffleMode(this->m_shuffle);
}

void StatusBar::CyclePlay() {
    if (this->m_playing) {
        tunePause();
    } else {
        tunePlay();
    }
}

void StatusBar::Prev() {
    tunePrev();
}

void StatusBar::Next() {
    tuneNext();
}

void StatusBar::Forward() {
    u32 next = std::min(this->m_stats.current_frame + (this->m_stats.total_frames / 10), this->m_stats.total_frames);
    tuneSeek(next);
}

void StatusBar::Backward() {
    u32 next = std::max(s64(this->m_stats.current_frame) - s64(this->m_stats.total_frames / 10), s64(0));
    tuneSeek(next);
}

bool StatusBar::CanEditPlaybackPolicy() const {
    return this->m_active_state != applet_bgm::SilentTitleId &&
           !applet_bgm::IsStartupState(this->m_active_state);
}

bool StatusBar::HasPlayableSession() const {
    return this->m_active_state != applet_bgm::SilentTitleId &&
           this->m_playlist_size != 0;
}

bool StatusBar::CanNavigateQueue() const {
    return HasPlayableSession() &&
           !applet_bgm::IsStartupState(this->m_active_state);
}

bool StatusBar::CanSeek() const {
    return HasPlayableSession() && this->m_stats.total_frames != 0;
}

const AlphaSymbol &StatusBar::GetPlaybackSymbol() {
    return this->m_playing ? symbol::pause::symbol : symbol::play::symbol;
}
