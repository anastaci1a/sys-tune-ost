#include "impl/music_player.hpp"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "impl/aud_wrapper.h"
#include "impl/source.hpp"
#include "qlaunch_scene_observer.hpp"
#include "album_video_observer.hpp"
#include "tune_service.hpp"
#include "tune_result.hpp"

extern "C" {
u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// TODO(TJ): calculate minimum heap
// TODO(TJ): calculate reasonable amount of heap for playlist entries.
void __libnx_initheap(void) {
    static char inner_heap[1024 * 250];
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit() {
    R_ABORT_UNLESS(smInitialize());
    // This is diagnostic and deliberately best-effort. The experimental NPDM
    // gives this startup thread priority over boot2 so the runtime-only future
    // MITM declaration is installed before qlaunch can acquire erpt:c.
    tune::qlaunch_scene::Initialize();
    tune::album_video::Initialize();
    // The priority bump is only needed for the startup handshake. Restore the
    // stable build's normal main-thread priority for the rest of the process.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x30);
    R_ABORT_UNLESS(setsysInitialize());
    {
        SetSysFirmwareVersion version;
        R_ABORT_UNLESS(setsysGetFirmwareVersion(&version));
        hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
        setsysExit();
    }

    R_ABORT_UNLESS(gpioInitialize());
    R_ABORT_UNLESS(fsInitialize());
    R_ABORT_UNLESS(audWrapperInitialize());
    R_ABORT_UNLESS(pm::Initialize());
    R_ABORT_UNLESS(sdmc::Open());
}

void __appExit(void) {
    tune::album_video::Exit();
    tune::qlaunch_scene::Exit();
    sdmc::Close();
    pm::Exit();
    audWrapperExit();
    fsExit();
    gpioExit();
    smExit();
}

} // extern "C"

namespace {

    alignas(0x1000) u8 gpioThreadBuffer[0x1000];
    // Routing now samples HID and several independent UI observers. Keep
    // enough headroom that an unusually deep service call cannot turn into a
    // stack-overflow freeze on the detector thread.
    alignas(0x1000) u8 pmdmntThreadBuffer[0x3000];
    alignas(0x1000) u8 tuneThreadBuffer[0x6000];

}

int main(int, char *[]) {
    R_ABORT_UNLESS(tune::impl::Initialize());

    /* Get GPIO session for the headphone jack pad. */
    GpioPadSession headphone_detect_session;
    R_ABORT_UNLESS(gpioOpenSession(&headphone_detect_session, GpioPadName(0x15)));

    ::Thread gpioThread;
    ::Thread pmdmtThread;
    ::Thread tuneThread;
    R_ABORT_UNLESS(threadCreate(&gpioThread, tune::impl::GpioThreadFunc, &headphone_detect_session, gpioThreadBuffer, sizeof(gpioThreadBuffer), 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&pmdmtThread, tune::impl::PmdmntThreadFunc, nullptr, pmdmntThreadBuffer, sizeof(pmdmntThreadBuffer), 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&tuneThread, tune::impl::TuneThreadFunc, nullptr, tuneThreadBuffer, sizeof(tuneThreadBuffer), 0x20, -2));

    R_ABORT_UNLESS(threadStart(&gpioThread));
    R_ABORT_UNLESS(threadStart(&pmdmtThread));
    R_ABORT_UNLESS(threadStart(&tuneThread));

    /* Create services */
    R_ABORT_UNLESS(tune::InitializeServer());
    tune::LoopProcess();
    R_ABORT_UNLESS(tune::ExitServer());

    tune::impl::Exit();
    svcCancelSynchronization(gpioThread.handle);

    R_ABORT_UNLESS(threadWaitForExit(&gpioThread));
    R_ABORT_UNLESS(threadWaitForExit(&pmdmtThread));
    R_ABORT_UNLESS(threadWaitForExit(&tuneThread));

    R_ABORT_UNLESS(threadClose(&gpioThread));
    R_ABORT_UNLESS(threadClose(&pmdmtThread));
    R_ABORT_UNLESS(threadClose(&tuneThread));

    tune::impl::Finalize();

    /* Close gpio session. */
    gpioPadClose(&headphone_detect_session);

    return 0;
}
