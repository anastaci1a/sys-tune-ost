#include "pm.hpp"
#include "applet_bgm.hpp"

namespace {

constexpr u64 QLAUNCH_TITLE_ID{0x0100000000001000ULL};
u64 CURRENT_TITLE_ID{};

}

namespace pm {

auto Initialize() -> Result {
    auto rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }

    return pminfoInitialize();
}

void Exit() {
    pminfoExit();
    pmdmntExit();
}

// SOURCE: https://github.com/retronx-team/sys-clk/blob/570f1e5fe10b253eff0c8fda1bb893bb620af052/sysmodule/src/process_management.cpp#L37
void getCurrentPidTid(u64* pid_out, u64* tid_out) {
    Result rc{};
    if (R_SUCCEEDED(rc = pmdmntGetApplicationProcessId(pid_out))) {
        if (0x20f == pminfoGetProgramId(tid_out, *pid_out)){
            *tid_out = QLAUNCH_TITLE_ID;
        }
    } else if (rc == 0x20f) {
        *tid_out = QLAUNCH_TITLE_ID;
    } else {
        *tid_out = CURRENT_TITLE_ID;
    }
}

void getAppletBgmTarget(u64* pid_out, u64* tid_out, bool application_out_of_focus) {
    *pid_out = 0;
    *tid_out = 0;

    // A library applet can be in front of a suspended application. Prefer the
    // applet process whenever one of the known targets is running.
    for (const auto title_id : applet_bgm::DetectionTitleIds) {
        u64 pid = 0;
        if (R_SUCCEEDED(pmdmntGetProcessId(&pid, title_id)) && pid != 0) {
            *pid_out = pid;
            *tid_out = title_id;
            return;
        }
    }

    // The application slot remains occupied while a game is suspended behind
    // HOME. pdm:qry supplies the missing focus state so qlaunch music can
    // resume without waiting for the game to close.
    u64 application_pid = 0;
    if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&application_pid)) && application_pid != 0) {
        *pid_out = application_pid;
        if (application_out_of_focus) {
            *tid_out = applet_bgm::QlaunchTitleId;
        }
        return;
    }

    *tid_out = applet_bgm::QlaunchTitleId;
}

auto PollCurrentPidTid(u64* pid_out, u64* tid_out) -> bool {
    getCurrentPidTid(pid_out, tid_out);

    if (*tid_out != CURRENT_TITLE_ID) {
        CURRENT_TITLE_ID = *tid_out;
        return true;
    }

    return false;
}

}
