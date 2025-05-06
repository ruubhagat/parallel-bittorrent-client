# Parallel BitTorrent Client (MPI + OpenMP) Team T8

This project is a demonstration of a parallel BitTorrent client implemented in C++ using MPI (Message Passing Interface) for process-level parallelism and OpenMP for multi-threading awareness and specific task parallelization. It utilizes the excellent [libtorrent-rasterbar](https://www.libtorrent.org/) library for the core BitTorrent protocol handling.

## Description

The client employs a coordinator-worker model using MPI:
*   **Rank 0 (Coordinator):** Takes the torrent file or magnet link as input, distributes the task to worker processes, waits for completion summaries, and prints a final summary table.
*   **Rank 1 to N-1 (Workers):** Each worker receives the torrent task, initializes an independent libtorrent session (listening on different ports), downloads the torrent into its own dedicated directory (`rank_X_download`), calculates download/upload statistics, performs basic peer analysis, and sends a summary back to the coordinator upon completion.

## Features

*   **Parallel Downloading:** Leverages multiple MPI processes to download the same torrent concurrently, potentially increasing overall throughput.
*   **MPI Communication:** Uses MPI for task distribution and collection of results.
*   **OpenMP Integration:**
    *   Compiled with OpenMP support.
    *   Configures libtorrent to utilize multiple threads for internal tasks (e.g., hashing).
    *   Demonstrates explicit OpenMP parallelization (`#pragma omp parallel for`) for the peer analysis task within each worker.
*   **Detailed Statistics:** Calculates and displays per-worker:
    *   Instantaneous Download/Upload Rates
    *   Average Download/Upload Rates (rolling window)
    *   Total Payload Downloaded/Uploaded (MiB)
    *   Upload/Download Ratio
*   **Basic Peer Management:**
    *   Periodically retrieves the list of connected peers.
    *   Analyzes peers based on their `failcount`.
    *   Disconnects peers exceeding a predefined `failcount` threshold.
*   **Final Summary Table:** Coordinator process prints a formatted table summarizing results from all workers.
*   **Input:** Supports both `.torrent` files and `magnet:` links.
*   **Resume Data:** Saves `.fastresume` data for each worker upon completion to allow resuming downloads later.

## Technologies Used

*   **C++17:** Core programming language.
*   **MPI:** For process-level parallelism (tested with Open MPI, should work with MPICH).
*   **OpenMP:** For multi-threading support within processes.
*   **libtorrent-rasterbar (v2.0.x):** Core BitTorrent library.
*   **Make:** For building the project.
*   **pkg-config:** For finding libtorrent library flags.

## Prerequisites

*   A C++17 compliant compiler (e.g., `g++` or `clang++`).
*   An MPI implementation (e.g., Open MPI, MPICH) providing `mpicxx` and `mpirun`.
*   `libtorrent-rasterbar` development library (version 2.0.x recommended). On Debian/Ubuntu: `sudo apt-get install libtorrent-rasterbar-dev`. On macOS with Homebrew: `brew install libtorrent-rasterbar`.
*   `pkg-config` utility. On Debian/Ubuntu: `sudo apt-get install pkg-config`. On macOS with Homebrew: `brew install pkg-config`.
*   `make` build utility (usually pre-installed on Linux/macOS).

## Building

1.  Clone the repository (if you haven't already).
2.  Navigate to the project's root directory in your terminal.
3.  **(Optional but Recommended)** Clean previous builds:
    ```bash
    make clean
    ```
4.  Compile the project:
    ```bash
    make
    ```
    This will use `mpicxx` (your MPI C++ wrapper) and the `Makefile` to compile the source code and link against libtorrent, OpenMP, and other necessary libraries. The final executable will be placed in `bin/parallel_torrent`.

## Running

Execute the client using `mpirun`, specifying the number of processes (`-np`) and the path to a `.torrent` file or a magnet link.

*   **N = Number of Processes:** One process (Rank 0) acts as the coordinator, the rest (N-1) act as workers. Minimum `N=1` (runs in single-process mode).
*   **Torrent Input:** Provide the full path to a `.torrent` file or the full `magnet:` link enclosed in double quotes.

**Examples:**

*   **Run with 4 processes (1 coordinator, 3 workers) using a torrent file:**
    ```bash
    mpirun -np 4 ./bin/parallel_torrent /path/to/your/download.torrent
    ```

*   **Run with 2 processes (1 coordinator, 1 worker) using a magnet link:**
    ```bash
    mpirun -np 4 ./bin/parallel_torrent "magnet:?xt=urn:btih:YOUR_TORRENT_INFO_HASH&dn=OptionalName&tr=udp..."
    ```

*   **Run in single-process mode (Rank 0 does the download):**
    ```bash
    mpirun -np 1 ./bin/parallel_torrent /path/to/your/download.torrent
    ```

*(Output directories like `rank_1_download`, `rank_2_download`, etc., will be created in the directory where you run the command)*.

## Notes

*   **Peer Disconnection Visibility:** The peer check runs every 15 seconds (configurable via `peer_check_interval`). For very fast downloads, the torrent might complete before this check is triggered, so you might not see the "Starting Peer Check" or "Disconnecting peers" logs. Use a larger or slower torrent to observe this feature.
*   **OpenMP Role:** OpenMP is primarily used implicitly by libtorrent for internal tasks. The explicit `#pragma omp parallel for` is included for demonstrating concurrent peer *analysis* within a worker, but the actual peer disconnection calls remain sequential.
*   **Independent Downloads:** Each worker downloads a full, independent copy of the torrent into its own directory (`rank_X_download`). This implementation does *not* coordinate piece downloading between MPI ranks to assemble a single shared copy.

