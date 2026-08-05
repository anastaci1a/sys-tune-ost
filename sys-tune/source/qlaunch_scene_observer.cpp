#include "qlaunch_scene_observer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace tune::qlaunch_scene {

namespace {

constexpr u64 QlaunchProgramId = 0x0100000000001000ULL;
constexpr u32 MitmQueryCommandId = 65000;
constexpr u32 SmInstallMitmCommandId = 65000;
constexpr u32 SmUninstallMitmCommandId = 65001;
constexpr u32 SmAcknowledgeMitmCommandId = 65003;
constexpr u32 SmDeclareFutureMitmCommandId = 65006;
constexpr u32 SmClearFutureMitmCommandId = 65007;

constexpr u32 ErptSubmitContextCommandId = 0;
constexpr u32 ErptSubmitMultipleContextCommandId = 6;
constexpr u32 ErptSystemAppletSceneCategory = 82;
constexpr u32 ErptSystemAppletSceneField = 218;
constexpr u8 ErptNumericU8FieldType = 12;

constexpr size_t MaxClientSessions = 10;
constexpr size_t ObserverStackSize = 0x4000;
constexpr size_t MaxHipcHandles = 16;

struct MitmProcessInfo {
    u64 process_id;
    u64 program_id;
    u64 keys_held;
    u64 flags;
};
static_assert(sizeof(MitmProcessInfo) == 0x20);

struct ErptFieldEntry {
    u32 id;
    u8 type;
    u8 reserved[3];
    union {
        u64 value_u64;
        u32 value_u32;
        u16 value_u16;
        u8 value_u8;
        s64 value_i64;
        s32 value_i32;
        s16 value_i16;
        s8 value_i8;
        bool value_bool;
        struct {
            u32 start_index;
            u32 size;
        } value_array;
    };
};
static_assert(sizeof(ErptFieldEntry) == 0x10);
static_assert(offsetof(ErptFieldEntry, value_u64) == 0x08);

struct ErptCategoryEntry {
    u32 category;
    u32 field_count;
    u32 array_buffer_count;
};
static_assert(sizeof(ErptCategoryEntry) == 0x0c);

struct ErptContextEntry {
    u32 version;
    u32 field_count;
    u32 category;
    u32 reserved;
    ErptFieldEntry fields[20];
    u64 array_buffer;
    u32 array_free_count;
    u32 array_buffer_size;
};
static_assert(sizeof(ErptContextEntry) == 0x160);
static_assert(offsetof(ErptContextEntry, fields) == 0x10);
static_assert(offsetof(ErptContextEntry, array_buffer) == 0x150);

constexpr size_t ErptLegacyCategoryCount = 0x10;
constexpr size_t ErptLegacyFieldCount = ErptLegacyCategoryCount * 4;

struct ErptMultipleCategoryContextEntry {
    u32 version;
    u32 category_count;
    u32 categories[ErptLegacyCategoryCount];
    u32 field_counts[ErptLegacyCategoryCount];
    u32 array_buffer_counts[ErptLegacyCategoryCount];
    ErptFieldEntry fields[ErptLegacyFieldCount];
};
static_assert(sizeof(ErptMultipleCategoryContextEntry) == 0x4c8);
static_assert(offsetof(ErptMultipleCategoryContextEntry, fields) == 0xc8);

struct ClientSession {
    Handle client_handle;
    Service forward_service;
    u64 process_id;
};

Mutex g_info_mutex{};
TuneQlaunchSceneObserverInfo g_info{};

Thread g_thread{};
bool g_thread_started{};
alignas(0x1000) u8 g_thread_stack[ObserverStackSize];

TipcService g_sm_session{};
bool g_sm_session_open{};
bool g_future_mitm_declared{};
Handle g_mitm_port{INVALID_HANDLE};
Handle g_query_session{INVALID_HANDLE};
ClientSession g_client_sessions[MaxClientSessions]{};

void SetStatus(TuneQlaunchObserverStatus status, Result result = 0) {
    mutexLock(&g_info_mutex);
    g_info.status = status;
    g_info.last_result = result;
    mutexUnlock(&g_info_mutex);
}

void RecordQuery(const MitmProcessInfo& process) {
    mutexLock(&g_info_mutex);
    ++g_info.query_count;
    if (process.program_id == QlaunchProgramId) {
        g_info.qlaunch_process_id = process.process_id;
    }
    mutexUnlock(&g_info_mutex);
}

void RecordScene(u8 scene) {
    mutexLock(&g_info_mutex);

    ++g_info.scene_update_count;
    g_info.current_scene = scene;
    g_info.has_scene = true;
    g_info.status = TuneQlaunchObserverStatus_ReceivingScenes;
    g_info.last_result = 0;

    const auto count = static_cast<size_t>(g_info.history_count);
    if (count == 0 || g_info.history[count - 1].scene != scene) {
        if (count == TuneQlaunchSceneHistorySize) {
            for (size_t i = 1; i < TuneQlaunchSceneHistorySize; ++i) {
                g_info.history[i - 1] = g_info.history[i];
            }
            g_info.history_count = TuneQlaunchSceneHistorySize - 1;
        }

        auto& event = g_info.history[g_info.history_count++];
        event = {};
        event.tick = armGetSystemTick();
        event.scene = scene;
    }

    mutexUnlock(&g_info_mutex);
}

void RecordClientRequest(bool parsed, u64 command_id) {
    mutexLock(&g_info_mutex);
    ++g_info.request_count;
    if (parsed) {
        g_info.last_command_id = static_cast<u32>(command_id);
        g_info.has_last_command = true;
        if (command_id == ErptSubmitContextCommandId ||
            command_id == ErptSubmitMultipleContextCommandId) {
            ++g_info.context_command_count;
        }
    }
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
    // DetachClient is best-effort during shutdown.
    tipcDispatch(service, 4, .in_send_pid = true);
    tipcClose(service);
}

Result InstallMitm() {
    Handle handles[2] = {INVALID_HANDLE, INVALID_HANDLE};
    const auto name = smEncodeName("erpt:c");
    Result result = tipcDispatchIn(&g_sm_session, SmInstallMitmCommandId, name,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove, SfOutHandleAttr_HipcMove},
        .out_handles = handles);
    if (R_SUCCEEDED(result)) {
        g_mitm_port = handles[0];
        g_query_session = handles[1];
    }
    return result;
}

Result DeclareFutureMitm() {
    const auto name = smEncodeName("erpt:c");
    const auto result = tipcDispatchIn(
        &g_sm_session, SmDeclareFutureMitmCommandId, name);
    if (R_SUCCEEDED(result)) {
        g_future_mitm_declared = true;
    }
    return result;
}

Result ClearFutureMitm() {
    const auto name = smEncodeName("erpt:c");
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
    const auto name = smEncodeName("erpt:c");
    tipcDispatchIn(&g_sm_session, SmUninstallMitmCommandId, name);
}

Result AcknowledgeMitm(Service* forward, MitmProcessInfo* process) {
    Handle handle = INVALID_HANDLE;
    const auto name = smEncodeName("erpt:c");
    Result result = tipcDispatchInOut(
        &g_sm_session,
        SmAcknowledgeMitmCommandId,
        name,
        *process,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove},
        .out_handles = &handle);
    if (R_SUCCEEDED(result)) {
        serviceCreate(forward, handle);
    }
    return result;
}

void PrepareResponse(Result result, const void* data = nullptr, size_t data_size = 0) {
    auto* base = static_cast<u8*>(armGetTls());
    std::memset(base, 0, 0x100);

    const auto word_count = static_cast<u32>(
        (sizeof(CmifOutHeader) + data_size + 0x10) / sizeof(u32));
    const auto response = hipcMakeRequestInline(base,
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
    if (R_SUCCEEDED(result) && data != nullptr && data_size != 0) {
        std::memcpy(header + 1, data, data_size);
    }
}

bool ParseCmifRequest(
    const HipcParsedRequest& request,
    u64* command_id,
    const void** raw_data,
    size_t* raw_data_size) {
    if (request.meta.num_data_words == 0 ||
        request.data.data_words == nullptr) {
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
    } else {
        return false;
    }

    *command_id = header->command_id;
    *raw_data = header + 1;
    *raw_data_size = payload_size - sizeof(*header);
    return true;
}

Result ReceiveRequest(Handle handle, HipcParsedRequest* request) {
    // erpt's server pointer-buffer size is zero, so qlaunch always sends the
    // real payloads as map aliases. Explicitly advertise no receive-static
    // buffer here as well instead of inheriting stale TLS descriptor state.
    hipcMakeRequestInline(
        armGetTls(), .type = CmifCommandType_Invalid);

    s32 index = -1;
    Result result = svcReplyAndReceive(&index, &handle, 1, 0, UINT64_MAX);
    if (R_SUCCEEDED(result)) {
        *request = hipcParseRequest(armGetTls());
    }
    return result;
}

Result Reply(Handle handle) {
    s32 index = -1;
    Result result = svcReplyAndReceive(&index, &handle, 0, handle, 0);
    return result == KERNELRESULT(TimedOut) ? 0 : result;
}

Result ProcessQuerySession() {
    HipcParsedRequest request{};
    Result result = ReceiveRequest(g_query_session, &request);
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
    if (!ParseCmifRequest(
            request, &command_id, &raw_data, &raw_data_size) ||
        command_id != MitmQueryCommandId ||
        raw_data_size < sizeof(MitmProcessInfo)) {
        PrepareResponse(MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return Reply(g_query_session);
    }

    const auto& process = *static_cast<const MitmProcessInfo*>(raw_data);
    RecordQuery(process);
    const bool should_mitm = process.program_id == QlaunchProgramId;
    PrepareResponse(0, &should_mitm, sizeof(should_mitm));
    return Reply(g_query_session);
}

void InspectContext(const void* buffer, size_t size) {
    if (buffer == nullptr || size < sizeof(ErptContextEntry)) {
        return;
    }

    const auto& context = *static_cast<const ErptContextEntry*>(buffer);
    if (context.category != ErptSystemAppletSceneCategory) {
        return;
    }

    const auto field_count = std::min<u32>(context.field_count, 20);
    for (u32 i = 0; i < field_count; ++i) {
        const auto& field = context.fields[i];
        if (field.id == ErptSystemAppletSceneField &&
            field.type == ErptNumericU8FieldType) {
            RecordScene(field.value_u8);
            return;
        }
    }
}

void InspectMultipleContext(const HipcParsedRequest& request) {
    if (request.meta.num_send_buffers < 3) {
        return;
    }

    const auto* categories = static_cast<const ErptCategoryEntry*>(
        hipcGetBufferAddress(&request.data.send_buffers[0]));
    const auto category_count =
        hipcGetBufferSize(&request.data.send_buffers[0]) /
        sizeof(ErptCategoryEntry);
    const auto* fields = static_cast<const ErptFieldEntry*>(
        hipcGetBufferAddress(&request.data.send_buffers[1]));
    const auto total_field_count =
        hipcGetBufferSize(&request.data.send_buffers[1]) /
        sizeof(ErptFieldEntry);

    if (categories == nullptr || fields == nullptr) {
        return;
    }

    size_t field_offset = 0;
    for (size_t i = 0; i < category_count; ++i) {
        const auto field_count = static_cast<size_t>(categories[i].field_count);
        if (field_count > total_field_count -
                std::min(field_offset, total_field_count)) {
            return;
        }

        if (categories[i].category == ErptSystemAppletSceneCategory) {
            for (size_t j = 0; j < field_count; ++j) {
                const auto& field = fields[field_offset + j];
                if (field.id == ErptSystemAppletSceneField &&
                    field.type == ErptNumericU8FieldType) {
                    RecordScene(field.value_u8);
                    return;
                }
            }
        }
        field_offset += field_count;
    }
}

void InspectLegacyMultipleContext(const HipcParsedRequest& request) {
    if (request.meta.num_send_buffers < 1) {
        return;
    }

    const auto* entry =
        static_cast<const ErptMultipleCategoryContextEntry*>(
            hipcGetBufferAddress(&request.data.send_buffers[0]));
    if (entry == nullptr ||
        hipcGetBufferSize(&request.data.send_buffers[0]) < sizeof(*entry)) {
        return;
    }

    const auto category_count = std::min<size_t>(
        entry->category_count, ErptLegacyCategoryCount);
    size_t field_offset = 0;
    for (size_t i = 0; i < category_count; ++i) {
        const auto field_count = static_cast<size_t>(entry->field_counts[i]);
        if (field_count > ErptLegacyFieldCount -
                std::min(field_offset, ErptLegacyFieldCount)) {
            return;
        }

        if (entry->categories[i] == ErptSystemAppletSceneCategory) {
            for (size_t j = 0; j < field_count; ++j) {
                const auto& field = entry->fields[field_offset + j];
                if (field.id == ErptSystemAppletSceneField &&
                    field.type == ErptNumericU8FieldType) {
                    RecordScene(field.value_u8);
                    return;
                }
            }
        }
        field_offset += field_count;
    }
}

void InspectErptRequest(const HipcParsedRequest& request) {
    u64 command_id = 0;
    const void* raw_data = nullptr;
    size_t raw_data_size = 0;
    const bool parsed = ParseCmifRequest(
        request, &command_id, &raw_data, &raw_data_size);
    RecordClientRequest(parsed, command_id);
    if (!parsed) {
        return;
    }

    if (command_id == ErptSubmitContextCommandId &&
        request.meta.num_send_buffers >= 1) {
        InspectContext(
            hipcGetBufferAddress(&request.data.send_buffers[0]),
            hipcGetBufferSize(&request.data.send_buffers[0]));
    } else if (command_id == ErptSubmitMultipleContextCommandId) {
        const auto first_buffer_size = request.meta.num_send_buffers >= 1
            ? hipcGetBufferSize(&request.data.send_buffers[0]) : 0;
        if (first_buffer_size >=
            sizeof(ErptMultipleCategoryContextEntry)) {
            InspectLegacyMultipleContext(request);
        } else if (request.meta.num_send_buffers >= 3) {
            InspectMultipleContext(request);
        }
    }
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
    if (!ParseCmifRequest(
            request, &command_id, &raw_data, &raw_data_size)) {
        return false;
    }

    // CopyFromCurrentDomain, CloneCurrentObject, and CloneCurrentObjectEx
    // each return a new session. Wrap that session too, otherwise SDK session
    // pools could silently bypass the observer after the first request.
    return command_id == 1 || command_id == 2 || command_id == 4;
}

ClientSession* WrapReturnedSession(
    ClientSession& parent,
    HipcResponse& response,
    Handle* proxy_client_handle) {
    if (response.num_move_handles < 1) {
        return nullptr;
    }

    auto* free_session = FindFreeClientSession();
    if (free_session == nullptr) {
        return nullptr;
    }

    Handle proxy_server = INVALID_HANDLE;
    Handle proxy_client = INVALID_HANDLE;
    const auto result = svcCreateSession(
        &proxy_server, &proxy_client, false, 0);
    if (R_FAILED(result)) {
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
    Result result = ReceiveRequest(session.client_handle, &request);
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

    if (request.meta.type == CmifCommandType_Request ||
        request.meta.type == CmifCommandType_RequestWithContext) {
        InspectErptRequest(request);
    }

    Handle request_copy_handles[MaxHipcHandles]{};
    const auto request_copy_count = std::min<size_t>(
        request.meta.num_copy_handles, MaxHipcHandles);
    for (size_t i = 0; i < request_copy_count; ++i) {
        request_copy_handles[i] = request.data.copy_handles[i];
    }

    const bool wrap_returned_session = IsControlReturningSession(request);
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

    const Result reply_result = Reply(session.client_handle);
    for (size_t i = 0; i < response_copy_count; ++i) {
        svcCloseHandle(response_copy_handles[i]);
    }
    if (R_FAILED(reply_result) && wrapped_session != nullptr) {
        svcCloseHandle(proxy_client_handle);
        CloseClientSession(*wrapped_session);
    }
    return R_FAILED(result) ? result : reply_result;
}

void CloseClientSession(ClientSession& session) {
    if (session.client_handle != INVALID_HANDLE) {
        svcCloseHandle(session.client_handle);
    }
    serviceClose(&session.forward_service);
    session = {};
}

Result AcceptClientSession() {
    ClientSession* free_session = FindFreeClientSession();
    if (free_session == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    MitmProcessInfo process{};
    Service forward{};
    Result result = AcknowledgeMitm(&forward, &process);
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
    g_info.qlaunch_process_id = process.process_id;
    g_info.status = TuneQlaunchObserverStatus_QlaunchConnected;
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
            SetStatus(TuneQlaunchObserverStatus_Failed, result);
            break;
        }

        const Handle signaled_handle = handles[signaled];
        if (signaled_handle == g_query_session) {
            result = ProcessQuerySession();
            if (R_FAILED(result)) {
                SetStatus(TuneQlaunchObserverStatus_Failed, result);
                break;
            }
        } else if (signaled_handle == g_mitm_port) {
            result = AcceptClientSession();
            if (R_FAILED(result)) {
                SetStatus(TuneQlaunchObserverStatus_Failed, result);
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

    // The runtime future declaration was cleared as soon as installation
    // completed. Removing the MITM therefore lets future clients connect
    // directly to erpt:c if the observer ever fails.
    UninstallMitm();
    CloseObserverHandles();
    ClosePrivateSmSession(&g_sm_session);
    g_sm_session_open = false;
}

}

Result Initialize() {
    mutexLock(&g_info_mutex);
    g_info = {};
    g_info.status = TuneQlaunchObserverStatus_Starting;
    mutexUnlock(&g_info_mutex);

    // Install synchronously during process startup. A runtime-only future MITM
    // declaration closes the race with qlaunch without leaving a persistent
    // mitm.lst file on the SD card; every handled failure clears it again.
    Result result = OpenPrivateSmSession(&g_sm_session);
    if (R_FAILED(result)) {
        SetStatus(TuneQlaunchObserverStatus_Unavailable, result);
        return result;
    }
    g_sm_session_open = true;

    result = DeclareFutureMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneQlaunchObserverStatus_Unavailable, result);
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    result = InstallMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneQlaunchObserverStatus_Unavailable, result);
        ClearFutureMitm();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    // Keep the runtime future declaration until both MITM handles are live, so
    // clients can never reach a half-built proxy.
    result = ClearFutureMitm();
    if (R_FAILED(result)) {
        SetStatus(TuneQlaunchObserverStatus_Failed, result);
        UninstallMitm();
        if (g_future_mitm_declared) {
            ClearFutureMitm();
        }
        CloseObserverHandles();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }
    SetStatus(TuneQlaunchObserverStatus_Installed);

    result = threadCreate(
        &g_thread,
        ObserverThread,
        nullptr,
        g_thread_stack,
        sizeof(g_thread_stack),
        0x20,
        -2);
    if (R_FAILED(result)) {
        SetStatus(TuneQlaunchObserverStatus_Failed, result);
        UninstallMitm();
        CloseObserverHandles();
        ClosePrivateSmSession(&g_sm_session);
        g_sm_session_open = false;
        return result;
    }

    result = threadStart(&g_thread);
    if (R_FAILED(result)) {
        threadClose(&g_thread);
        SetStatus(TuneQlaunchObserverStatus_Failed, result);
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

TuneQlaunchSceneObserverInfo GetInfo() {
    mutexLock(&g_info_mutex);
    const auto info = g_info;
    mutexUnlock(&g_info_mutex);
    return info;
}

void ResetHistory() {
    mutexLock(&g_info_mutex);
    g_info.history_count = 0;
    std::memset(g_info.history, 0, sizeof(g_info.history));
    mutexUnlock(&g_info_mutex);
}

}
