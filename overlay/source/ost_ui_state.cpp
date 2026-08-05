#include "ost_ui_state.hpp"

#include "tune.h"

#include <vector>

namespace ost_ui_state {

namespace {

struct PlaylistState {
    u64 title_id{};
    u32 revision{};
    bool reload_pending{};
};

std::vector<PlaylistState> g_playlist_states;
u32 g_global_revision{};

PlaylistState* findState(u64 title_id) {
    for (auto& state : g_playlist_states) {
        if (state.title_id == title_id) {
            return &state;
        }
    }
    return nullptr;
}

PlaylistState& findOrCreateState(u64 title_id) {
    if (auto* state = findState(title_id)) {
        return *state;
    }
    return g_playlist_states.emplace_back(PlaylistState{.title_id = title_id});
}

bool flush(PlaylistState& state) {
    if (!state.reload_pending) {
        return true;
    }
    if (R_FAILED(tuneReloadOstState(state.title_id))) {
        return false;
    }
    state.reload_pending = false;
    return true;
}

}

u32 markPlaylistChanged(u64 title_id) {
    if (++g_global_revision == 0) {
        ++g_global_revision;
    }
    auto& state = findOrCreateState(title_id);
    state.revision = g_global_revision;
    state.reload_pending = true;
    return state.revision;
}

u32 getGlobalRevision() {
    return g_global_revision;
}

u32 getPlaylistRevision(u64 title_id) {
    const auto* state = findState(title_id);
    return state ? state->revision : 0;
}

void flushPlaylistReload(u64 title_id) {
    if (auto* state = findState(title_id)) {
        flush(*state);
    }
}

void flushAllPlaylistReloads() {
    for (auto& state : g_playlist_states) {
        flush(state);
    }
}

}
