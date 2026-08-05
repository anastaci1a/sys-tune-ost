#include "album_video_observer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace tune::album_video {

namespace {

constexpr u64 AlbumProgramId = 0x010000000000100DULL;
constexpr u32 MitmQueryCommandId = 65000;
constexpr u32 SmInstallMitmCommandId = 65000;
constexpr u32 SmUninstallMitmCommandId = 65001;
constexpr u32 SmAcknowledgeMitmCommandId = 65003;
constexpr u32 SmDeclareFutureMitmCommandId = 65006;
constexpr u32 SmClearFutureMitmCommandId = 65007;

constexpr u32 CapsOpenAccessorSessionCommandId = 60002;
constexpr u32 CapsOpenMovieStreamCommandId = 2001;
constexpr u32 CapsCloseMovieStreamCommandId = 2002;
constexpr u32 CapsReadMovieDataCommandId = 2004;

constexpr size_t MaxClientSessions = 12;
constexpr size_t MaxActiveStreams = 8;
constexpr size_t ObserverStackSize = 0x4000;
constexpr size_t MaxHipcHandles = 16;

struct MitmProcessInfo {
    u64 process_id;
    u64 program_id;
    u64 keys_held;
    u64 flags;
};
static_assert(sizeof(MitmProcessInfo) == 0x20);

struct ClientSession {
    Handle client_handle;
    Service forward_service;
    u64 process_id;
};

struct ActiveStream {
    u64 stream_handle;
    Handle owner_session;
};

enum class MovieAction {
    None,
    Open,
    Read,
    Close,
};

struct InspectedRequest {
    bool parsed{};
    bool is_domain{};
    u64 command_id{};
    MovieAction action{MovieAction::None};
    u64 stream_handle{};
};

Mutex g_info_mutex{};
TuneAlbumVideoObserverInfo g_info{};
ActiveStream g_active_streams[MaxActiveStreams]{};

Thread g_thread{};
bool g_thread_started{};
alignas(0x1000) u8 g_thread_stack[ObserverStackSize];

TipcService g_sm_session{};
bool g_sm_session_open{};
bool g_future_mitm_declared{};
Handle g_mitm_port{INVALID_HANDLE};
Handle g_query_session{INVALID_HANDLE};
ClientSession g_client_sessions[MaxClientSessions]{};

void SetStatus(TuneAlbumVideoObserverStatus status, Result result = 0) {
    mutexLock(&g_info_mutex);
    g_info.status = status;
    g_info.last_result = result;
    mutexUnlock(&g_info_mutex);
}

void UpdateActiveStateLocked() {
    u32 count = 0;
    for (const auto& stream : g_active_streams) {
        if (stream.stream_handle != 0) {
            ++count;
        }
    }

    const bool active = count != 0;
    if (g_info.video_active != active) {
        g_info.video_active = active;
        ++g_info.state_update_count;
    }
    g_info.active_stream_count = count;
}

void RecordQuery(const MitmProcessInfo& process) {
    mutexLock(&g_info_mutex);
    ++g_info.query_count;
    if (process.program_id == AlbumProgramId) {
        g_info.album_process_id = process.process_id;
    }
    mutexUnlock(&g_info_mutex);
}

void RecordRequest(const InspectedRequest& request, Handle owner_session) {
    mutexLock(&g_info_mutex);
    ++g_info.request_count;
    if (request.parsed) {
        g_info.last_command_id = static_cast<u32>(request.command_id);
        g_info.has_last_command = true;
    }

    switch (request.action) {
        case MovieAction::Open:
            ++g_info.movie_open_count;
            g_info.has_signal = true;
            break;
        case MovieAction::Read: {
            ++g_info.movie_read_count;
            g_info.has_signal = true;
            bool found = false;
            for (auto& stream : g_active_streams) {
                if (stream.stream_handle == request.stream_handle) {
                    stream.owner_session = owner_session;
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (auto& stream : g_active_streams) {
                    if (stream.stream_handle == 0) {
                        stream.stream_handle = request.stream_handle;
                        stream.owner_session = owner_session;
                        break;
                    }
                }
            }
            g_info.status =
                TuneAlbumVideoObserverStatus_ReceivingMovieData;
            UpdateActiveStateLocked();
            break;
        }
        case MovieAction::Close:
            ++g_info.movie_close_count;
            g_info.has_signal = true;
            for (auto& stream : g_active_streams) {
                if (stream.stream_handle == request.stream_handle) {
                    stream = {};
                }
            }
            UpdateActiveStateLocked();
            break;
        case MovieAction::None:
            break;
    }
    mutexUnlock(&g_info_mutex);
}

void RemoveStreamsForSession(Handle owner_session) {
    mutexLock(&g_info_mutex);
    for (auto& stream : g_active_streams) {
        if (stream.owner_session == owner_session) {
            stream = {};
        }
    }
    UpdateActiveStateLocked();
    mutexUnlock(&g_info_mutex);
}

Result OpenPrivateSmSession(TipcService* out) {
    Handle handle = INVALID_HANDLE;
    Result result = svcConnectToNamedPort(&handle, "sm:");
    if (R_FAILED(result)) {
        return result;
    }

    tipcCreate(out, handle);
    result = tipcDispatch(out, 0, .in_send_pid = true);
    if (R_FAILED(result)) {
        tipcClose(out);
    }
    return result;
}

void ClosePrivateSmSession(TipcService* service) {
    tipcDispatch(service, 4, .in_send_pid = true);
    tipcClose(service);
}

Result InstallMitm() {
    Handle handles[2] = {INVALID_HANDLE, INVALID_HANDLE};
    const auto name = smEncodeName("caps:a");
    const auto result = tipcDispatchIn(
        &g_sm_session, SmInstallMitmCommandId, name,
        .out_handle_attrs = {
            SfOutHandleAttr_HipcMove, SfOutHandleAttr_HipcMove},
        .out_handles = handles);
    if (R_SUCCEEDED(result)) {
        g_mitm_port = handles[0];
        g_query_session = handles[1];
    }
    return result;
}

Result DeclareFutureMitm() {
    const auto name = smEncodeName("caps:a");
    const auto result = tipcDispatchIn(
        &g_sm_session, SmDeclareFutureMitmCommandId, name);
    if (R_SUCCEEDED(result)) {
        g_future_mitm_declared = true;
    }
    return result;
}

Result ClearFutureMitm() {
    const auto name = smEncodeName("caps:a");
    const auto result = tipcDispatchIn(
        &g_sm_session, SmClearFutureMitmCommandId, name);
    if (R_SUCCEEDED(result)) {
        g_future_mitm_declared = false;
    }
    return result;
}

void UninstallMitm() {
    if (!g_sm_session_open) {
        return;
    }
    const auto name = smEncodeName("caps:a");
    tipcDispatchIn(&g_sm_session, SmUninstallMitmCommandId, name);
}

Result AcknowledgeMitm(Service* forward, MitmProcessInfo* process) {
    Handle handle = INVALID_HANDLE;
    const auto name = smEncodeName("caps:a");
    const auto result = tipcDispatchInOut(
        &g_sm_session, SmAcknowledgeMitmCommandId, name, *process,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove},
        .out_handles = &handle);
    if (R_SUCCEEDED(result)) {
        serviceCreate(forward, handle);
    }
    return result;
}

void PrepareResponse(
    Result result, const void* data = nullptr, size_t data_size = 0) {
    auto* base = static_cast<u8*>(armGetTls());
    std::memset(base, 0, 0x100);

    const auto word_count = static_cast<u32>(
        (sizeof(CmifOutHeader) + data_size + 0x10) / sizeof(u32));
    const auto response = hipcMakeRequestInline(
        base,
        .type = CmifCommandType_Request,
        .num_data_words = word_count);

    auto* header = static_cast<CmifOutHeader*>(
        cmifGetAlignedDataStart(response.data_words, base));
    *header = {
        .magic = CMIF_OUT_HEADER_MAGIC,
        .version = 0,
        .result = result,
        .token = 0,
    };
    if (R_SUCCEEDED(result) && data && data_size != 0) {
        std::memcpy(header + 1, data, data_size);
    }
}

bool ParseCmifRequest(
    const HipcParsedRequest& request, u64* command_id,
    const void** raw_data, size_t* raw_data_size, bool* is_domain) {
    if (request.meta.num_data_words == 0 || !request.data.data_words) {
        return false;
    }

    const auto data_size = request.meta.num_data_words * sizeof(u32);
    auto* const data_words = reinterpret_cast<u8*>(request.data.data_words);
    auto* const aligned = static_cast<u8*>(
        cmifGetAlignedDataStart(request.data.data_words, armGetTls()));
    const auto data_address = reinterpret_cast<uintptr_t>(data_words);
    const auto aligned_address = reinterpret_cast<uintptr_t>(aligned);
    if (aligned_address < data_address ||
        aligned_address - data_address > data_size) {
        return false;
    }

    const auto aligned_size = data_size -
        static_cast<size_t>(aligned_address - data_address);
    const CmifInHeader* header = nullptr;
    size_t payload_size = aligned_size;
    *is_domain = false;

    if (aligned_size >= sizeof(CmifInHeader) &&
        reinterpret_cast<const CmifInHeader*>(aligned)->magic ==
            CMIF_IN_HEADER_MAGIC) {
        header = reinterpret_cast<const CmifInHeader*>(aligned);
    } else if (aligned_size >=
               sizeof(CmifDomainInHeader) + sizeof(CmifInHeader)) {
        const auto* domain =
            reinterpret_cast<const CmifDomainInHeader*>(aligned);
        const auto domain_size = static_cast<size_t>(domain->data_size);
        if (domain->type != CmifDomainRequestType_SendMessage ||
            domain_size < sizeof(CmifInHeader) ||
            domain_size > aligned_size - sizeof(CmifDomainInHeader)) {
            return false;
        }
        header = reinterpret_cast<const CmifInHeader*>(domain + 1);
        if (header->magic != CMIF_IN_HEADER_MAGIC) {
            return false;
        }
        payload_size = domain_size;
        *is_domain = true;
    } else {
        return false;
    }

    *command_id = header->command_id;
    *raw_data = header + 1;
    *raw_data_size = payload_size - sizeof(*header);
    return true;
}

Result GetForwardedResponseResult(bool is_domain) {
    const auto response = hipcParseResponse(armGetTls());
    if (!response.data_words || response.num_data_words == 0) {
        return MAKERESULT(Module_Libnx, LibnxError_InvalidCmifOutHeader);
    }

    auto* start = static_cast<u8*>(
        cmifGetAlignedDataStart(response.data_words, armGetTls()));
    if (is_domain) {
        start += sizeof(CmifDomainOutHeader);
    }
    const auto* header = reinterpret_cast<const CmifOutHeader*>(start);
    if (header->magic != CMIF_OUT_HEADER_MAGIC) {
        return MAKERESULT(Module_Libnx, LibnxError_InvalidCmifOutHeader);
    }
    return header->result;
}

Result ReceiveRequest(Handle handle, HipcParsedRequest* request) {
    // caps:a movie operations use raw data and map-alias buffers. Explicitly
    // clear any stale receive-static descriptors before accepting a request.
    hipcMakeRequestInline(armGetTls(), .type = CmifCommandType_Invalid);

    s32 index = -1;
    const auto result =
        svcReplyAndReceive(&index, &handle, 1, 0, UINT64_MAX);
    if (R_SUCCEEDED(result)) {
        *request = hipcParseRequest(armGetTls());
    }
    return result;
}

Result Reply(Handle handle) {
    s32 index = -1;
    const auto result = svcReplyAndReceive(&index, &handle, 0, handle, 0);
    return result == KERNELRESULT(TimedOut) ? 0 : result;
}

Result ProcessQuerySession() {
    HipcParsedRequest request{};
    auto result = ReceiveRequest(g_query_session, &request);
    if (R_FAILED(result)) {
        return result;
    }

    if (request.meta.type == CmifCommandType_Close) {
        PrepareResponse(0);
        Reply(g_query_session);
        return KERNELRESULT(ConnectionClosed);
    }

    u64 command_id = 0;
    const void* raw_data = nullptr;
    size_t raw_data_size = 0;
    bool is_domain = false;
    if (!ParseCmifRequest(
            request, &command_id, &raw_data, &raw_data_size, &is_domain) ||
        command_id != MitmQueryCommandId ||
        raw_data_size < sizeof(MitmProcessInfo)) {
        PrepareResponse(MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return Reply(g_query_session);
    }

    const auto& process = *static_cast<const MitmProcessInfo*>(raw_data);
    RecordQuery(process);
    const bool should_mitm = process.program_id == AlbumProgramId;
    PrepareResponse(0, &should_mitm, sizeof(should_mitm));
    return Reply(g_query_session);
}

InspectedRequest InspectRequest(const HipcParsedRequest& request) {
    InspectedRequest inspected{};
    const void* raw_data = nullptr;
    size_t raw_data_size = 0;
    inspected.parsed = ParseCmifRequest(
        request, &inspected.command_id, &raw_data, &raw_data_size,
        &inspected.is_domain);
    if (!inspected.parsed) {
        return inspected;
    }

    if (inspected.command_id == CapsOpenMovieStreamCommandId) {
        inspected.action = MovieAction::Open;
    } else if (inspected.command_id == CapsReadMovieDataCommandId &&
               raw_data_size >= sizeof(u64)) {
        inspected.action = MovieAction::Read;
        std::memcpy(&inspected.stream_handle, raw_data, sizeof(u64));
    } else if (inspected.command_id == CapsCloseMovieStreamCommandId &&
               raw_data_size >= sizeof(u64)) {
        inspected.action = MovieAction::Close;
        std::memcpy(&inspected.stream_handle, raw_data, sizeof(u64));
    }
    return inspected;
}

void PrepareForwardedPid(const HipcParsedRequest& request) {
    if (!request.meta.send_pid) {
        return;
    }

    constexpr u64 MitmProcessIdTag = 0xFFFE000000000000ULL;
    constexpr u64 OldProcessIdMask = 0x0000FFFFFFFFFFFFULL;
    auto* process_id = reinterpret_cast<u64*>(
        reinterpret_cast<uintptr_t>(armGetTls()) +
        sizeof(HipcHeader) + sizeof(HipcSpecialHeader));
    *process_id = MitmProcessIdTag | (*process_id & OldProcessIdMask);
}

ClientSession* FindFreeClientSession() {
    for (auto& session : g_client_sessions) {
        if (session.client_handle == INVALID_HANDLE) {
            return &session;
        }
    }
    return nullptr;
}

bool IsControlReturningSession(const HipcParsedRequest& request) {
    if (request.meta.type != CmifCommandType_Control &&
        request.meta.type != CmifCommandType_ControlWithContext) {
        return false;
    }

    u64 command_id = 0;
    const void* raw_data = nullptr;
    size_t raw_data_size = 0;
    bool is_domain = false;
    if (!ParseCmifRequest(
            request, &command_id, &raw_data, &raw_data_size, &is_domain)) {
        return false;
    }
    return command_id == 1 || command_id == 2 || command_id == 4;
}

ClientSession* WrapReturnedSession(
    ClientSession& parent, HipcResponse& response,
    Handle* proxy_client_handle) {
    if (response.num_move_handles < 1) {
        return nullptr;
    }

    auto* free_session = FindFreeClientSession();
    if (!free_session) {
        return nullptr;
    }

    Handle proxy_server = INVALID_HANDLE;
    Handle proxy_client = INVALID_HANDLE;
    if (R_FAILED(svcCreateSession(
            &proxy_server, &proxy_client, false, 0))) {
        return nullptr;
    }

    Service forward{};
    forward.session = response.move_handles[0];
    forward.own_handle = true;
    forward.pointer_buffer_size =
        parent.forward_service.pointer_buffer_size;

    free_session->client_handle = proxy_server;
    free_session->forward_service = forward;
    free_session->process_id = parent.process_id;

    response.move_handles[0] = proxy_client;
    *proxy_client_handle = proxy_client;
    return free_session;
}

void CloseClientSession(ClientSession& session);

Result ProcessClientSession(ClientSession& session, bool* closed) {
    *closed = false;

    HipcParsedRequest request{};
    auto result = ReceiveRequest(session.client_handle, &request);
    if (R_FAILED(result)) {
        *closed = true;
        return result;
    }

    if (request.meta.type == CmifCommandType_Close) {
        PrepareResponse(0);
        Reply(session.client_handle);
        *closed = true;
        return 0;
    }

    InspectedRequest inspected{};
    if (request.meta.type == CmifCommandType_Request ||
        request.meta.type == CmifCommandType_RequestWithContext) {
        inspected = InspectRequest(request);
    }

    Handle request_copy_handles[MaxHipcHandles]{};
    const auto request_copy_count = std::min<size_t>(
        request.meta.num_copy_handles, MaxHipcHandles);
    for (size_t i = 0; i < request_copy_count; ++i) {
        request_copy_handles[i] = request.data.copy_handles[i];
    }

    const bool wraps_accessor = inspected.parsed &&
        inspected.command_id == CapsOpenAccessorSessionCommandId;
    const bool wrap_returned_session =
        wraps_accessor || IsControlReturningSession(request);
    PrepareForwardedPid(request);
    result = svcSendSyncRequest(session.forward_service.session);
    for (size_t i = 0; i < request_copy_count; ++i) {
        svcCloseHandle(request_copy_handles[i]);
    }

    Handle response_copy_handles[MaxHipcHandles]{};
    size_t response_copy_count = 0;
    Handle proxy_client_handle = INVALID_HANDLE;
    ClientSession* wrapped_session = nullptr;
    if (R_FAILED(result)) {
        PrepareResponse(result);
        *closed = true;
    } else {
        const auto command_result =
            GetForwardedResponseResult(inspected.is_domain);
        if (R_SUCCEEDED(command_result)) {
            RecordRequest(inspected, session.client_handle);
        } else if (inspected.parsed) {
            // Retain request visibility without treating a failed stream
            // operation as an active movie.
            auto failed = inspected;
            failed.action = MovieAction::None;
            RecordRequest(failed, session.client_handle);
        }

        auto response = hipcParseResponse(armGetTls());
        response_copy_count = std::min<size_t>(
            response.num_copy_handles, MaxHipcHandles);
        for (size_t i = 0; i < response_copy_count; ++i) {
            response_copy_handles[i] = response.copy_handles[i];
        }
        if (wrap_returned_session) {
            wrapped_session = WrapReturnedSession(
                session, response, &proxy_client_handle);
        }
    }

    const auto reply_result = Reply(session.client_handle);
    for (size_t i = 0; i < response_copy_count; ++i) {
        svcCloseHandle(response_copy_handles[i]);
    }
    if (R_FAILED(reply_result) && wrapped_session) {
        svcCloseHandle(proxy_client_handle);
        CloseClientSession(*wrapped_session);
    }
    return R_FAILED(result) ? result : reply_result;
}

void CloseClientSession(ClientSession& session) {
    RemoveStreamsForSession(session.client_handle);
    if (session.client_handle != INVALID_HANDLE) {
        svcCloseHandle(session.client_handle);
    }
    serviceClose(&session.forward_service);
    session = {};
}

Result AcceptClientSession() {
    auto* free_session = FindFreeClientSession();
    if (!free_session) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    MitmProcessInfo process{};
    Service forward{};
    auto result = AcknowledgeMitm(&forward, &process);
    if (R_FAILED(result)) {
        return result;
    }

    Handle client = INVALID_HANDLE;
    result = svcAcceptSession(&client, g_mitm_port);
    if (R_FAILED(result)) {
        serviceClose(&forward);
        return result;
    }

    free_session->client_handle = client;
    free_session->forward_service = forward;
    free_session->process_id = process.process_id;

    mutexLock(&g_info_mutex);
    g_info.album_process_id = process.process_id;
    g_info.status = TuneAlbumVideoObserverStatus_AlbumConnected;
    g_info.last_result = 0;
    mutexUnlock(&g_info_mutex);
    return 0;
}

void CloseObserverHandles() {
    for (auto& session : g_client_sessions) {
        CloseClientSession(session);
    }
    if (g_query_session != INVALID_HANDLE) {
        svcCloseHandle(g_query_session);
        g_query_session = INVALID_HANDLE;
    }
    if (g_mitm_port != INVALID_HANDLE) {
        svcCloseHandle(g_mitm_port);
        g_mitm_port = INVALID_HANDLE;
    }
}

void ObserverThread(void*) {
    Result result = 0;
    while (true) {
        Handle handles[2 + MaxClientSessions]{};
        size_t handle_count = 0;
        handles[handle_count++] = g_query_session;
        handles[handle_count++] = g_mitm_port;
        for (const auto& session : g_client_sessions) {
            if (session.client_handle != INVALID_HANDLE) {
                handles[handle_count++] = session.client_handle;
            }
        }

        s32 signaled = -1;
        result = svcWaitSynchronization(
            &signaled, handles, handle_count, UINT64_MAX);
        if (result == KERNELRESULT(Cancelled)) {
            break;
        }
        if (R_FAILED(result) || signaled < 0 ||
            static_cast<size_t>(signaled) >= handle_count) {
            SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
            break;
        }

        const auto signaled_handle = handles[signaled];
        if (signaled_handle == g_query_session) {
            result = ProcessQuerySession();
            if (R_FAILED(result)) {
                SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
                break;
            }
        } else if (signaled_handle == g_mitm_port) {
            result = AcceptClientSession();
            if (R_FAILED(result)) {
                SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
                break;
            }
        } else {
            for (auto& session : g_client_sessions) {
                if (session.client_handle != signaled_handle) {
                    continue;
                }
                bool closed = false;
                result = ProcessClientSession(session, &closed);
                if (closed || R_FAILED(result)) {
                    CloseClientSession(session);
                }
                break;
            }
        }
    }

    UninstallMitm();
    CloseObserverHandles();
    ClosePrivateSmSession(&g_sm_session);
    g_sm_session_open = false;
}

}

Result Initialize() {
    mutexLock(&g_info_mutex);
    g_info = {};
    std::memset(g_active_streams, 0, sizeof(g_active_streams));
    g_info.status = TuneAlbumVideoObserverStatus_Starting;
    mutexUnlock(&g_info_mutex);

    auto result = OpenPrivateSmSession(&g_sm_session);
    if (R_FAILED(result)) {
        SetStatus(TuneAlbumVideoObserverStatus_Unavailable, result);
        return result;
    }
    g_sm_session_open = true;

    result = DeclareFutureMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneAlbumVideoObserverStatus_Unavailable, result);
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    result = InstallMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneAlbumVideoObserverStatus_Unavailable, result);
        ClearFutureMitm();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    result = ClearFutureMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
        UninstallMitm();
        if (g_future_mitm_declared) {
            ClearFutureMitm();
        }
        CloseObserverHandles();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }
    SetStatus(TuneAlbumVideoObserverStatus_Installed);

    result = threadCreate(
        &g_thread, ObserverThread, nullptr, g_thread_stack,
        sizeof(g_thread_stack), 0x20, -2);
    if (R_FAILED(result)) {
        SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
        UninstallMitm();
        CloseObserverHandles();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    result = threadStart(&g_thread);
    if (R_FAILED(result)) {
        threadClose(&g_thread);
        SetStatus(TuneAlbumVideoObserverStatus_Failed, result);
        UninstallMitm();
        CloseObserverHandles();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }
    g_thread_started = true;
    return 0;
}

void Exit() {
    if (!g_thread_started) {
        return;
    }
    svcCancelSynchronization(g_thread.handle);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_thread_started = false;
}

TuneAlbumVideoObserverInfo GetInfo() {
    mutexLock(&g_info_mutex);
    const auto info = g_info;
    mutexUnlock(&g_info_mutex);
    return info;
}

Snapshot GetSnapshot() {
    mutexLock(&g_info_mutex);
    Snapshot snapshot{
        .availability = Availability::Waiting,
        .active = g_info.video_active != 0,
        .update_count = g_info.state_update_count,
    };
    if (g_info.status == TuneAlbumVideoObserverStatus_Unavailable ||
        g_info.status == TuneAlbumVideoObserverStatus_Failed) {
        snapshot.availability = Availability::Unavailable;
    } else if (g_info.has_signal) {
        snapshot.availability = Availability::Ready;
    }
    mutexUnlock(&g_info_mutex);
    return snapshot;
}

}
