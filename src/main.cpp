// src/main.cpp
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <iterator>
#include <memory>
#include <cstring>
#include <numeric>
#include <iomanip> // For std::fixed, std::setprecision, std::setw
#include <deque>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <cerrno>
#include <vector>
#include <sstream>
#include <map>     // For storing worker results
#include <cmath>   // For std::sin in dummy OpenMP calculation

// --- MPI Header ---
#include <mpi.h>

// --- OpenMP Header ---
#include <omp.h>

// --- libtorrent headers ---
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/fwd.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/close_reason.hpp>

namespace lt = libtorrent;

// Structure for worker summary data
struct WorkerSummary {
    long long total_download_bytes;
    long long total_upload_bytes;
    char completion_status; // 'D' = Done, 'F' = Failed
};

// Function to convert torrent state enum to string
std::string state_to_string(lt::torrent_status::state_t s) {
    switch (s) {
        case lt::torrent_status::checking_files: return "checking";
        case lt::torrent_status::downloading_metadata: return "dl metadata";
        case lt::torrent_status::downloading: return "downloading";
        case lt::torrent_status::finished: return "finished";
        case lt::torrent_status::seeding: return "seeding";
        case lt::torrent_status::checking_resume_data: return "checking resume";
        default: return "<unknown>";
    }
}

// Helper for calculating rolling average rates
struct RateTracker {
    std::deque<std::pair<std::chrono::steady_clock::time_point, float>> history;
    std::chrono::seconds window_duration = std::chrono::seconds(10);

    void add_rate(float rate) {
        auto now = std::chrono::steady_clock::now();
        history.emplace_back(now, rate);
        while (!history.empty() && (now - history.front().first > window_duration)) {
            history.pop_front();
        }
    }

    float get_average_rate() const {
        if (history.size() < 2) return 0.0f;
        float total_rate = 0.0f;
        for (const auto& p : history) { total_rate += p.second; }
        return total_rate / history.size();
    }
};

// ================================================================
// Torrent Download Logic (Worker)
// ================================================================
char download_torrent(const std::string& torrent_input, int mpi_rank, WorkerSummary& summary_data_out) {
    // --- Session Setup ---
    std::cout << "[Rank " << mpi_rank << "] Starting download for: " << torrent_input.substr(0, 70) << "..." << std::endl;
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::user_agent, "MyParallelClient/1.6-MPI+OMP" + std::to_string(mpi_rank)); // Version bump
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_upnp, true);
    settings.set_bool(lt::settings_pack::enable_natpmp, true);
    int base_port = 6881; int rank_port = base_port + (mpi_rank * 2); if (rank_port > 65530) rank_port = base_port + (mpi_rank % 100);
    std::string listen_interfaces_str = "0.0.0.0:" + std::to_string(rank_port) + ",[::]:" + std::to_string(rank_port);
    settings.set_str(lt::settings_pack::listen_interfaces, listen_interfaces_str);
    int num_hashing_threads = std::max(1, omp_get_max_threads() / 2); settings.set_int(lt::settings_pack::hashing_threads, num_hashing_threads);
    std::cout << "[Rank " << mpi_rank << "] Configured libtorrent internal threads: " << num_hashing_threads << " (OMP aware)." << std::endl;
    settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage | lt::alert_category::connect | lt::alert_category::peer | lt::alert_category::stats | lt::alert_category::peer_log);
    lt::session_params params(settings); lt::session ses(params); lt::torrent_handle h;
    std::cout << "[Rank " << mpi_rank << "] libtorrent session started. Listening on: " << listen_interfaces_str << std::endl;

    // --- Add Torrent ---
    lt::add_torrent_params atp; atp.save_path = "./rank_" + std::to_string(mpi_rank) + "_download";
    try { std::filesystem::path save_dir(atp.save_path); if (!std::filesystem::exists(save_dir)) { std::filesystem::create_directories(save_dir); } std::cout << "[Rank " << mpi_rank << "] Using save directory: " << std::filesystem::absolute(save_dir).string() << std::endl; } catch (const std::filesystem::filesystem_error& e) { std::cerr << "[Rank " << mpi_rank << "] FS error: " << e.what() << std::endl; return 'F'; }
    lt::error_code ec; if (torrent_input.rfind("magnet:?", 0) == 0) { lt::parse_magnet_uri(torrent_input, atp, ec); if (ec) { std::cerr << "[Rank " << mpi_rank << "] Error parsing magnet: " << ec.message() << std::endl; return 'F'; } } else { atp.ti = std::make_shared<lt::torrent_info>(torrent_input, std::ref(ec)); if (ec || !atp.ti || !atp.ti->is_valid()) { std::cerr << "[Rank " << mpi_rank << "] Error loading torrent file: " << ec.message() << std::endl; return 'F'; } std::cout << "[Rank " << mpi_rank << "] Torrent file loaded: " << atp.ti->name() << std::endl; }
    ses.async_add_torrent(std::move(atp));

    // --- Main Loop Variables ---
    bool torrent_added_confirmed = false; bool torrent_finished = false; int loop_count = 0; const int max_loops_without_progress = 300; int loops_since_last_progress = 0; long long last_total_downloaded = 0;
    RateTracker download_rate_tracker; RateTracker upload_rate_tracker;

    // --- Main Loop ---
    while (!torrent_finished && loops_since_last_progress < max_loops_without_progress ) {
        loop_count++;
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);

        // ***** ADDED: OpenMP Demonstration: Dummy Parallel Work *****
        if (loop_count % 30 == 5 && torrent_added_confirmed) { // Run this infrequent dummy task (e.g., every 30s after torrent added)
            #pragma omp parallel
            {
                #pragma omp master
                {
                    std::cout << "[Rank " << mpi_rank << " OMP Master] Starting dummy OpenMP parallel calculation with " << omp_get_num_threads() << " threads." << std::endl;
                }

                double thread_local_sum = 0.0; // Each thread has its own sum
                #pragma omp for // Distribute loop iterations
                for (int i = 0; i < 500000; ++i) { // Reduced iterations for quicker demo
                    thread_local_sum += std::sin(static_cast<double>(i) * 0.0001 + omp_get_thread_num());
                }

                // Optionally, do something with thread_local_sum here, e.g., print it under a critical section
                // #pragma omp critical
                // {
                //    std::cout << "[Rank " << mpi_rank << " OMP Thread " << omp_get_thread_num() << "] Dummy calc sum: " << thread_local_sum << std::endl;
                // }
            } // End OpenMP parallel region
            std::cout << "[Rank " << mpi_rank << " OMP Master] Finished dummy OpenMP parallel calculation." << std::endl;
        }
        // ***** END: OpenMP Demonstration *****


        // --- Process Alerts ---
        for (lt::alert* a : alerts) {
            if (auto ata = lt::alert_cast<lt::add_torrent_alert>(a)) { if (ata->error) { std::cerr << "[Rank " << mpi_rank << "] Failed add: " << ata->error.message() << std::endl; return 'F'; } h = ata->handle; lt::torrent_status st = h.status(); std::cout << "[Rank " << mpi_rank << "] >>> Torrent added" << (st.has_metadata ? ": " + st.name : " (pending metadata).") << std::endl; torrent_added_confirmed = true; h.resume(); }
            else if (auto mra = lt::alert_cast<lt::metadata_received_alert>(a)) { h = mra->handle; torrent_added_confirmed = true; lt::torrent_status st = h.status(); std::cout << "[Rank " << mpi_rank << "] >>> Metadata received for: " << st.name << std::endl; }
            else if (auto saa = lt::alert_cast<lt::session_stats_alert>(a)) { (void)saa; }
            else if (auto sua = lt::alert_cast<lt::state_update_alert>(a)) { if (torrent_added_confirmed && h.is_valid() && !sua->status.empty()) { for(const auto& st : sua->status) { if (st.handle == h) { download_rate_tracker.add_rate(st.download_payload_rate); upload_rate_tracker.add_rate(st.upload_payload_rate); if (loop_count % 5 == 1) { std::cout << "[Rank " << mpi_rank << "] --- Status (" << (st.has_metadata ? st.name : "<no meta>") << ") ---" << std::endl; std::cout << "  State: " << state_to_string(st.state) << " (" << std::fixed << std::setprecision(1) << (st.progress * 100.0f) << "%) | Peers: " << st.num_peers << " (Seeds: " << st.num_seeds << ")" << std::endl; std::cout << "  Rate DL: " << std::fixed << std::setprecision(1) << (st.download_payload_rate / 1024.0f) << " KiB/s (Avg: " << (download_rate_tracker.get_average_rate() / 1024.0f) << ") | UL: " << (st.upload_payload_rate / 1024.0f) << " KiB/s (Avg: " << (upload_rate_tracker.get_average_rate() / 1024.0f) << ")" << std::endl; std::cout << "  Total DL: " << std::fixed << std::setprecision(2) << (st.total_payload_download / 1024.0f / 1024.0f) << " MiB | UL: " << (st.total_payload_upload / 1024.0f / 1024.0f) << " MiB | Ratio: " << ((st.total_payload_download > 0) ? (double)st.total_payload_upload / st.total_payload_download : 0.0) << std::endl; std::cout << "---------------------------------------" << std::endl; } if (st.total_payload_download > last_total_downloaded) { last_total_downloaded = st.total_payload_download; loops_since_last_progress = 0; } else if (st.state == lt::torrent_status::downloading) { loops_since_last_progress++; } if (!torrent_finished && (st.state == lt::torrent_status::finished || st.state == lt::torrent_status::seeding)) { torrent_finished = true; std::cout << "[Rank " << mpi_rank << "] >>> Torrent finished/seeding: " << st.name << std::endl; } break; } } } }
            else if (auto tfa = lt::alert_cast<lt::torrent_finished_alert>(a)) { if (tfa->handle == h) { std::cout << "[Rank " << mpi_rank << "] >>> Alert: Torrent finished: " << tfa->torrent_name() << std::endl; torrent_finished = true; } }
            else if (auto pea = lt::alert_cast<lt::peer_error_alert>(a)) { std::cerr << "[Rank " << mpi_rank << "] Peer Error: " << pea->endpoint << " : " << pea->error.message() << std::endl; }
            else if (auto pda = lt::alert_cast<lt::peer_disconnected_alert>(a)) { std::cout << "[Rank " << mpi_rank << "] Peer Disconnected: " << pda->endpoint << " ReasonCode: " << static_cast<int>(pda->reason) << " Err: " << pda->error.message() << std::endl; }
            else if (lt::alert_cast<lt::save_resume_data_alert>(a) || lt::alert_cast<lt::save_resume_data_failed_alert>(a)) { }
            else { /* Optional: log unhandled alerts */ }
        } // End alert processing loop

        // --- Post Updates ---
        if (torrent_added_confirmed && h.is_valid() && loop_count % 5 == 0) { ses.post_torrent_updates(); ses.post_session_stats(); }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (loops_since_last_progress >= max_loops_without_progress) { std::cerr << "[Rank " << mpi_rank << "] Timeout: No progress. Exiting.\n"; break; }
    } // End while loop

    // --- Post-loop summary ---
    if (loops_since_last_progress >= max_loops_without_progress) { std::cerr << "[Rank " << mpi_rank << "] Loop exited due to no progress.\n"; } else if (torrent_finished) { std::cout << "[Rank " << mpi_rank << "] Loop finished normally.\n"; } else { std::cout << "[Rank " << mpi_rank << "] Loop finished (reason unspecified).\n"; }

    // --- 5. Session Shutdown ---
    std::cout << "[Rank " << mpi_rank << "] Stopping session..." << std::endl; char final_result_status = 'F';
    summary_data_out.completion_status = 'F'; summary_data_out.total_download_bytes = 0; summary_data_out.total_upload_bytes = 0;
    if (torrent_added_confirmed && h.is_valid()) { std::cout << "[Rank " << mpi_rank << "] Saving resume data..." << std::endl; h.save_resume_data(lt::torrent_handle::save_info_dict); bool resume_saved = false; auto start = std::chrono::steady_clock::now(); while (!resume_saved && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) { std::vector<lt::alert*> alerts; ses.pop_alerts(&alerts); for (lt::alert* a : alerts) { if (auto sra = lt::alert_cast<lt::save_resume_data_alert>(a)) { if (sra->handle == h) { std::filesystem::path resume_filepath; try { std::filesystem::path save_dir(atp.save_path); std::string base_filename; lt::torrent_status st = h.status(); if (st.has_metadata && !st.name.empty()) { std::string safe_name = st.name; std::replace_if(safe_name.begin(), safe_name.end(), [](char c){ return std::string("/\\:*?\"<>|").find(c) != std::string::npos; }, '_'); if (safe_name.length() > 100) safe_name = safe_name.substr(0, 100); base_filename = ".resume_" + safe_name + ".fastresume"; } else { base_filename = ".resume_data_rank" + std::to_string(mpi_rank) + ".fastresume"; } resume_filepath = save_dir / base_filename; std::ofstream of(resume_filepath, std::ios::binary | std::ios::trunc); if (of) { auto const b = write_resume_data_buf(sra->params); of.write(b.data(), b.size()); std::cout << "[Rank " << mpi_rank << "] Resume data saved to " << resume_filepath.string() << std::endl; } else { std::cerr << "[Rank " << mpi_rank << "] Failed open resume file: " << resume_filepath.string() << " (errno: " << errno << ")" << std::endl; } } catch (const std::exception& e) { std::cerr << "[Rank " << mpi_rank << "] Ex saving resume: " << e.what() << std::endl; } resume_saved = true; break; } } else if (auto srfa = lt::alert_cast<lt::save_resume_data_failed_alert>(a)) { if (srfa->handle == h) { std::cerr << "[Rank " << mpi_rank << "] Failed save resume data (alert): " << srfa->error.message() << std::endl; resume_saved = true; break; } } } if (!resume_saved) std::this_thread::sleep_for(std::chrono::milliseconds(200)); } if (!resume_saved) { std::cerr << "[Rank " << mpi_rank << "] Timeout waiting for resume save alert.\n"; } lt::torrent_status final_st = h.status(); final_result_status = (final_st.state == lt::torrent_status::finished || final_st.state == lt::torrent_status::seeding) ? 'D' : ((loops_since_last_progress >= max_loops_without_progress) ? 'F' : 'D'); if (final_result_status == 'D' && !(final_st.state == lt::torrent_status::finished || final_st.state == lt::torrent_status::seeding)) { std::cout << "[Rank " << mpi_rank << "] Final state: " << state_to_string(final_st.state) << std::endl; } summary_data_out.completion_status = final_result_status; summary_data_out.total_download_bytes = final_st.total_payload_download; summary_data_out.total_upload_bytes = final_st.total_payload_upload; } else { std::cerr << "[Rank " << mpi_rank << "] No valid handle. Result=F\n"; final_result_status = 'F'; summary_data_out.completion_status = 'F'; }
    std::cout << "[Rank " << mpi_rank << "] Pausing session..." << std::endl; ses.pause(); std::cout << "[Rank " << mpi_rank << "] Session shutdown initiated." << std::endl; return final_result_status;
}

// ================================================================
// Main function with MPI setup
// ================================================================
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    int world_size; MPI_Comm_size(MPI_COMM_WORLD, &world_size); int world_rank; MPI_Comm_rank(MPI_COMM_WORLD, &world_rank); char processor_name[MPI_MAX_PROCESSOR_NAME]; int name_len; MPI_Get_processor_name(processor_name, &name_len);

    // --- Header Printing ---
    if (world_rank == 0) {
         std::cout << "========================================" << std::endl;
         std::cout << " Parallel BitTorrent Client (MPI+OMP) " << std::endl;
         std::cout << " MPI World Size: " << world_size << std::endl;
         #pragma omp parallel
         {
             #pragma omp master
             { std::cout << " OpenMP Max Threads: " << omp_get_num_threads() << std::endl; }
         }
         std::cout << "========================================" << std::endl;
    }
    MPI_Barrier(MPI_COMM_WORLD); std::cout << "[MPI Rank " << world_rank << "/" << world_size << "] Started on " << processor_name << std::endl;

    // --- Argument Handling ---
    std::string torrent_input_arg; if (world_rank == 0) { if (argc != 2) { std::cerr << "[Rank 0] Error: Usage...\n"; if (world_size > 1) { char sig = 0; for (int i = 1; i < world_size; ++i) MPI_Send(&sig, 0, MPI_CHAR, i, 1, MPI_COMM_WORLD); } MPI_Finalize(); return 1; } torrent_input_arg = argv[1]; std::cout << "[Rank 0] Input: " << torrent_input_arg.substr(0, 100) << (torrent_input_arg.length() > 100 ? "..." : "") << std::endl; }

    // --- MPI Logic ---
    if (world_rank == 0) {
        // --- Rank 0: Coordinator ---
        std::cout << "[Rank 0] Coordinator process." << std::endl; int num_workers = world_size - 1;
        if (num_workers > 0) {
            std::cout << "[Rank 0] Distributing task to " << num_workers << " worker(s)..." << std::endl; for (int i = 1; i < world_size; ++i) { std::cout << "[Rank 0] Sending task to Rank " << i << std::endl; MPI_Send(torrent_input_arg.c_str(), torrent_input_arg.length() + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD); }
            int completed = 0, succeeded = 0, failed = 0; std::map<int, WorkerSummary> worker_results; std::cout << "[Rank 0] Waiting for " << num_workers << " completion summaries..." << std::endl;
            while (completed < num_workers) { WorkerSummary received_summary; MPI_Status status; MPI_Recv(&received_summary, sizeof(WorkerSummary), MPI_BYTE, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, &status); int source_rank = status.MPI_SOURCE; completed++; worker_results[source_rank] = received_summary; if (received_summary.completion_status == 'D') { succeeded++; std::cout << "[Rank 0] Received SUCCESS summary from Rank " << source_rank << ". (" << completed << "/" << num_workers << ")" << std::endl; } else { failed++; std::cout << "[Rank 0] Received FAILURE/OTHER summary from Rank " << source_rank << ". (" << completed << "/" << num_workers << ")" << std::endl; } }
            std::cout << "[Rank 0] All workers reported." << std::endl;
            // --- Print Summary Table ---
            std::cout << "\n==================== Final Summary Per Worker ===================" << std::endl; std::cout << std::left << std::setw(8) << "Rank" << std::setw(20) << "Total Down (MiB)" << std::setw(20) << "Total Up (MiB)" << std::setw(10) << "Ratio" << std::setw(10) << "Status" << std::endl; std::cout << "-----------------------------------------------------------------" << std::endl; for (const auto& pair : worker_results) { int rank = pair.first; const WorkerSummary& summary = pair.second; double dl_mib = static_cast<double>(summary.total_download_bytes) / (1024.0 * 1024.0); double ul_mib = static_cast<double>(summary.total_upload_bytes) / (1024.0 * 1024.0); double ratio = (summary.total_download_bytes > 0) ? static_cast<double>(summary.total_upload_bytes) / summary.total_download_bytes : 0.0; std::string status_str = (summary.completion_status == 'D') ? "Success" : "Failed"; std::cout << std::left << std::setw(8) << rank << std::fixed << std::setprecision(2) << std::setw(20) << dl_mib << std::fixed << std::setprecision(2) << std::setw(20) << ul_mib << std::fixed << std::setprecision(2) << std::setw(10) << ratio << std::setw(10) << status_str << std::endl; } std::cout << "=================================================================\n" << std::endl;
            std::cout << "[Rank 0] Overall Result: " << succeeded << " succeeded, " << failed << " failed/stalled." << std::endl;
        } else { // Single process mode
            std::cout << "[Rank 0] Running in single process mode." << std::endl; WorkerSummary single_summary; char result = download_torrent(torrent_input_arg, world_rank, single_summary); std::cout << "[Rank 0] Download function finished with result: '" << result << "'" << std::endl;
            std::cout << "\n==================== Final Summary Per Worker ===================" << std::endl; std::cout << std::left << std::setw(8) << "Rank" << std::setw(20) << "Total Down (MiB)" << std::setw(20) << "Total Up (MiB)" << std::setw(10) << "Ratio" << std::setw(10) << "Status" << std::endl; std::cout << "-----------------------------------------------------------------" << std::endl; double dl_mib = static_cast<double>(single_summary.total_download_bytes) / (1024.0 * 1024.0); double ul_mib = static_cast<double>(single_summary.total_upload_bytes) / (1024.0 * 1024.0); double ratio = (single_summary.total_download_bytes > 0) ? static_cast<double>(single_summary.total_upload_bytes) / single_summary.total_download_bytes : 0.0; std::string status_str = (single_summary.completion_status == 'D') ? "Success" : "Failed"; std::cout << std::left << std::setw(8) << world_rank << std::fixed << std::setprecision(2) << std::setw(20) << dl_mib << std::fixed << std::setprecision(2) << std::setw(20) << ul_mib << std::fixed << std::setprecision(2) << std::setw(10) << ratio << std::setw(10) << status_str << std::endl; std::cout << "=================================================================\n" << std::endl;
        }
    } else {
        // --- Rank > 0: Worker ---
        MPI_Status status; MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == 0) { // Task
            int count; MPI_Get_count(&status, MPI_CHAR, &count); std::vector<char> buf(count); MPI_Recv(buf.data(), count, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); std::string received_input(buf.data()); std::cout << "[Rank " << world_rank << "] Received task." << std::endl;
            WorkerSummary summary_to_send;
            download_torrent(received_input, world_rank, summary_to_send);
            std::cout << "[Rank " << world_rank << "] Task finished. Sending summary (Status: '" << summary_to_send.completion_status << "') to Rank 0." << std::endl;
            MPI_Send(&summary_to_send, sizeof(WorkerSummary), MPI_BYTE, 0, 2, MPI_COMM_WORLD);
        } else if (status.MPI_TAG == 1) { /* Shutdown handling */ char dummy; MPI_Recv(&dummy, 0, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE); std::cout << "[Rank " << world_rank << "] Received shutdown signal. Exiting." << std::endl; }
        else { /* Unexpected Tag handling */ std::cerr << "[Rank " << world_rank << "] Unexpected tag " << status.MPI_TAG << "\n"; int count; MPI_Get_count(&status, MPI_CHAR, &count); std::vector<char> buf(count > 0 ? count : 1); MPI_Recv(buf.data(), count, MPI_CHAR, 0, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); WorkerSummary fail_summary; fail_summary.completion_status = 'F'; fail_summary.total_download_bytes = 0; fail_summary.total_upload_bytes = 0; MPI_Send(&fail_summary, sizeof(WorkerSummary), MPI_BYTE, 0, 2, MPI_COMM_WORLD); }
    }

    // --- Finalize ---
    MPI_Barrier(MPI_COMM_WORLD); if (world_rank == 0) { std::cout << "[Rank 0] All processes reached barrier. Finalizing MPI." << std::endl; } std::this_thread::sleep_for(std::chrono::milliseconds(50 * world_rank)); std::cout << "[Rank " << world_rank << "] Reached MPI_Finalize." << std::endl; MPI_Finalize(); return 0;
} // End main
