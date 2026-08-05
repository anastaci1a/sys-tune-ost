#include "gui_album_video_diagnostics.hpp"

#include "elm_text_block.hpp"

#include <cstdio>
#include <string>

namespace {

const char* StatusText(u32 status) {
    switch (status) {
        case TuneAlbumVideoObserverStatus_Unavailable:
            return "Unavailable";
        case TuneAlbumVideoObserverStatus_Starting:
            return "Starting";
        case TuneAlbumVideoObserverStatus_Installed:
            return "Installed — Waiting";
        case TuneAlbumVideoObserverStatus_AlbumConnected:
            return "Album Connected";
        case TuneAlbumVideoObserverStatus_ReceivingMovieData:
            return "Receiving Movie Data";
        case TuneAlbumVideoObserverStatus_Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

std::string ResultText(Result result) {
    if (R_SUCCEEDED(result)) {
        return "Success";
    }
    char text[16]{};
    std::snprintf(
        text, sizeof(text), "2%03u-%04u",
        R_MODULE(result), R_DESCRIPTION(result));
    return text;
}

std::string ProcessIdText(u64 process_id) {
    if (process_id == 0) {
        return "Not Connected";
    }
    char text[24]{};
    std::snprintf(
        text, sizeof(text), "0x%016llX",
        static_cast<unsigned long long>(process_id));
    return text;
}

std::string CommandText(u32 command_id) {
    char text[20]{};
    std::snprintf(text, sizeof(text), "0x%X (%u)", command_id, command_id);
    return text;
}

void SetValue(
    tsl::elm::ListItem* item, const std::string& value, bool faint = false) {
    if (item->getValue() != value) {
        item->setValue(value, faint);
    }
}

}

tsl::elm::Element* AlbumVideoDiagnosticsGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Album Video Observer"));
    list->addItem(new ElmTextBlock(
        "Tracks PhotoViewer movie-stream reads and closes.\n"
        "Playback and trimming should both register here."));

    m_status = new tsl::elm::ListItem("Observer Status");
    m_result = new tsl::elm::ListItem("Last Result");
    m_pid = new tsl::elm::ListItem("Album Process");
    m_video_state = new tsl::elm::ListItem("Video State");
    m_active_streams = new tsl::elm::ListItem("Active Movie Streams");
    list->addItem(m_status);
    list->addItem(m_result);
    list->addItem(m_pid);
    list->addItem(m_video_state);
    list->addItem(m_active_streams);

    list->addItem(new tsl::elm::CategoryHeader("Signal Counters"));
    m_queries = new tsl::elm::ListItem("MITM Queries");
    m_requests = new tsl::elm::ListItem("Album Requests");
    m_opens = new tsl::elm::ListItem("Movie Stream Opens");
    m_reads = new tsl::elm::ListItem("Movie Data Reads");
    m_closes = new tsl::elm::ListItem("Movie Stream Closes");
    m_state_updates = new tsl::elm::ListItem("Playback State Changes");
    m_last_command = new tsl::elm::ListItem("Last Command");
    list->addItem(m_queries);
    list->addItem(m_requests);
    list->addItem(m_opens);
    list->addItem(m_reads);
    list->addItem(m_closes);
    list->addItem(m_state_updates);
    list->addItem(m_last_command);

    frame->setDescription("\uE0E1 Back");
    frame->setContent(list);
    refresh();
    return frame;
}

void AlbumVideoDiagnosticsGui::refresh() {
    TuneAlbumVideoObserverInfo info{};
    const auto result = tuneGetAlbumVideoObserver(&info);
    if (R_FAILED(result)) {
        SetValue(m_status, "IPC Error", true);
        SetValue(m_result, ResultText(result));
        return;
    }

    SetValue(m_status, StatusText(info.status),
        info.status == TuneAlbumVideoObserverStatus_Unavailable ||
        info.status == TuneAlbumVideoObserverStatus_Failed);
    SetValue(m_result, ResultText(info.last_result),
        R_SUCCEEDED(info.last_result));
    SetValue(m_pid, ProcessIdText(info.album_process_id),
        info.album_process_id == 0);
    SetValue(m_video_state,
        info.has_signal ? (info.video_active ? "Active" : "Idle")
                        : "Not Seen",
        !info.has_signal);
    SetValue(m_active_streams,
        std::to_string(info.active_stream_count));
    SetValue(m_queries, std::to_string(info.query_count));
    SetValue(m_requests, std::to_string(info.request_count));
    SetValue(m_opens, std::to_string(info.movie_open_count));
    SetValue(m_reads, std::to_string(info.movie_read_count));
    SetValue(m_closes, std::to_string(info.movie_close_count));
    SetValue(m_state_updates, std::to_string(info.state_update_count));
    SetValue(m_last_command,
        info.has_last_command ? CommandText(info.last_command_id) : "Not Seen",
        !info.has_last_command);
}

void AlbumVideoDiagnosticsGui::update() {
    if ((m_tick++ % 15) == 0) {
        refresh();
    }
}
