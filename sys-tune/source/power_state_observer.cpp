#include "power_state_observer.hpp"

#include "impl/music_player.hpp"

#include <atomic>

namespace tune::power_state {

namespace {

// PSC reserves a 128-entry module namespace. 0x7e is intentionally outside
// every Nintendo module ID currently assigned through HOS 22.x, immediately
// below SPSM's fixed 0x7f slot.
constexpr u32 ObserverModuleId = 0x7e;
constexpr size_t ObserverStackSize = 0x3000;
constexpr u64 AudioQuiesceTimeoutNs = 150'000'000ULL;

Mutex g_info_mutex{};
TunePowerStateObserverInfo g_info{};

PscPmModule g_module{};
bool g_module_open{};
bool g_pscm_open{};

Thread g_thread{};
bool g_thread_started{};
std::atomic_bool g_stop = false;
alignas(0x1000) u8 g_thread_stack[ObserverStackSize];

void SetStatus(TunePowerObserverStatus status, Result result = 0) {
    mutexLock(&g_info_mutex);
    g_info.status = status;
    g_info.last_result = result;
    mutexUnlock(&g_info_mutex);
}

void CloseRegistration() {
    if (g_module_open) {
        // Finalize is best-effort. Closing the object/session is the important
        // fallback: PSC must never remain waiting on a failed observer.
        pscPmModuleFinalize(&g_module);
        eventClose(&g_module.event);
        pscPmModuleClose(&g_module);
        g_module_open = false;
    }
    if (g_pscm_open) {
        pscmExit();
        g_pscm_open = false;
    }
}

Transition ClassifyTransition(
    PscPmState state, PscPmState previous, bool has_previous,
    bool* low_power) {
    if (!has_previous) {
        if (state == PscPmState_Awake) {
            *low_power = false;
            return Transition::None;
        }

        // Starting during any partial-power state is safest to treat as a
        // sleep preparation. FullAwake will produce the matching wake later.
        *low_power = true;
        return Transition::Sleep;
    }

    switch (state) {
        case PscPmState_Awake:
            if (*low_power) {
                *low_power = false;
                return Transition::Wake;
            }
            break;

        case PscPmState_ReadyAwakenCritical:
            if (*low_power) {
                *low_power = false;
                return Transition::Wake;
            }
            break;

        case PscPmState_ReadyAwaken:
            // MinimumAwake is used in both directions. Its predecessor makes
            // it unambiguous: FullAwake -> MinimumAwake is power-down; a
            // partial-power state -> MinimumAwake is power-up.
            if (previous == PscPmState_Awake) {
                if (!*low_power) {
                    *low_power = true;
                    return Transition::Sleep;
                }
            } else if (*low_power) {
                *low_power = false;
                return Transition::Wake;
            }
            break;

        case PscPmState_ReadySleep:
        case PscPmState_ReadySleepCritical:
        case PscPmState_ReadyShutdown:
            if (!*low_power) {
                *low_power = true;
                return Transition::Sleep;
            }
            break;
    }

    return Transition::None;
}

void RecordRequest(
    PscPmState state, u32 flags, Transition transition,
    bool audio_quiesced) {
    mutexLock(&g_info_mutex);
    g_info.current_state = static_cast<u32>(state);
    g_info.current_flags = flags;
    g_info.has_state = true;
    g_info.last_state_tick = armGetSystemTick();
    ++g_info.request_count;
    if (transition != Transition::None) {
        g_info.last_transition = static_cast<u8>(transition);
        ++g_info.transition_count;
        if (transition == Transition::Sleep) {
            ++g_info.sleep_count;
            if (!audio_quiesced) {
                ++g_info.audio_quiesce_timeout_count;
            }
        } else {
            ++g_info.wake_count;
        }
    }
    mutexUnlock(&g_info_mutex);
}

void ObserverThread(void*) {
    bool has_previous = false;
    bool low_power = false;
    PscPmState previous = PscPmState_Awake;

    while (!g_stop.load(std::memory_order_acquire)) {
        const auto wait_result = eventWait(&g_module.event, UINT64_MAX);
        if (R_FAILED(wait_result)) {
            if (!g_stop.load(std::memory_order_acquire)) {
                SetStatus(TunePowerObserverStatus_Failed, wait_result);
                CloseRegistration();
            }
            break;
        }

        PscPmState state{};
        u32 flags = 0;
        auto result = pscPmModuleGetRequest(&g_module, &state, &flags);
        if (R_FAILED(result)) {
            SetStatus(TunePowerObserverStatus_Failed, result);
            CloseRegistration();
            break;
        }

        const auto transition = ClassifyTransition(
            state, previous, has_previous, &low_power);
        bool audio_quiesced = true;
        if (transition == Transition::Sleep) {
            const auto request = impl::PrepareForPowerSleep();
            audio_quiesced = impl::WaitForPowerAudioQuiesced(
                request, AudioQuiesceTimeoutNs);
        } else if (transition == Transition::Wake) {
            impl::NotifyPowerWake();
        }

        RecordRequest(state, flags, transition, audio_quiesced);
        previous = state;
        has_previous = true;

        result = pscPmModuleAcknowledge(&g_module, state);
        if (R_FAILED(result)) {
            SetStatus(TunePowerObserverStatus_Failed, result);
            CloseRegistration();
            break;
        }
    }
}

}

Result Initialize() {
    mutexLock(&g_info_mutex);
    g_info = {};
    g_info.status = TunePowerObserverStatus_Starting;
    g_info.module_id = ObserverModuleId;
    mutexUnlock(&g_info_mutex);
    g_stop.store(false, std::memory_order_release);

    auto result = pscmInitialize();
    if (R_FAILED(result)) {
        SetStatus(TunePowerObserverStatus_Unavailable, result);
        return result;
    }
    g_pscm_open = true;

    const u32 dependencies[] = {
        static_cast<u32>(PscPmModuleId_Fs),
        static_cast<u32>(PscPmModuleId_Audio),
    };
    result = pscmGetPmModule(
        &g_module, static_cast<PscPmModuleId>(ObserverModuleId),
        dependencies, sizeof(dependencies) / sizeof(dependencies[0]), true);
    if (R_FAILED(result)) {
        SetStatus(TunePowerObserverStatus_Failed, result);
        pscmExit();
        g_pscm_open = false;
        return result;
    }
    g_module_open = true;

    result = threadCreate(
        &g_thread, ObserverThread, nullptr, g_thread_stack,
        sizeof(g_thread_stack), 0x20, -2);
    if (R_FAILED(result)) {
        SetStatus(TunePowerObserverStatus_Failed, result);
        CloseRegistration();
        return result;
    }

    SetStatus(TunePowerObserverStatus_Active);
    result = threadStart(&g_thread);
    if (R_FAILED(result)) {
        threadClose(&g_thread);
        SetStatus(TunePowerObserverStatus_Failed, result);
        CloseRegistration();
        return result;
    }
    g_thread_started = true;
    return 0;
}

void Exit() {
    g_stop.store(true, std::memory_order_release);
    if (g_thread_started) {
        svcCancelSynchronization(g_thread.handle);
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_thread_started = false;
    }
    CloseRegistration();
}

TunePowerStateObserverInfo GetInfo() {
    mutexLock(&g_info_mutex);
    auto info = g_info;
    mutexUnlock(&g_info_mutex);
    info.lock_screen_enabled = impl::IsLockScreenEnabled();
    info.audio_hold_active = impl::IsPowerAudioHoldActive();
    return info;
}

Snapshot GetSnapshot() {
    mutexLock(&g_info_mutex);
    Snapshot snapshot{
        .availability =
            g_info.status == TunePowerObserverStatus_Active
            ? Availability::Active : Availability::Unavailable,
        .last_transition =
            static_cast<Transition>(g_info.last_transition),
        .transition_count = g_info.transition_count,
    };
    mutexUnlock(&g_info_mutex);
    return snapshot;
}

}
