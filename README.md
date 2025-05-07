# Parallel BitTorrent Client (MPI + OpenMP)

This project implements a BitTorrent client in C++ that leverages both MPI (Message Passing Interface) for distributed-memory parallelism and OpenMP for shared-memory parallelism. The primary goal is to distribute download and upload tasks to maximize throughput. It utilizes the [libtorrent-rasterbar](https://www.libtorrent.org/) library for core BitTorrent protocol handling.

## Overview

The client operates on a coordinator-worker model using MPI:

*   **Rank 0 (Coordinator):**
    *   Parses the torrent file or magnet link provided as a command-line argument.
    *   Distributes the download task to all available worker processes.
    *   Waits for completion summaries from each worker.
    *   Aggregates and displays a final summary table detailing the performance of each worker.
      
*   **Rank 1 to N-1 (Workers):**
    *   Each worker receives the torrent task from the coordinator.
    *   Initializes an independent `libtorrent::session` on a unique network port.
    *   Downloads the torrent into a dedicated, rank-specific directory (e.g., `rank_X_download/`).
    *   Actively calculates and tracks download/upload statistics, including rolling averages.
    *   Sends a summary of its activity (total bytes transferred, completion status) back to the coordinator.

The project emphasizes demonstrating a hybrid parallel approach and accurately measuring performance metrics.

## Core Features

*   **Hybrid Parallelism:**
    *   **MPI:** Distributes the task of downloading a torrent across multiple processes. Each process manages an independent BitTorrent session.
    *   **OpenMP:**
        *   Configures `libtorrent` to utilize multiple threads for its internal operations (e.g., hashing pieces), leveraging available CPU cores within each MPI worker process.
        *   Includes a demonstrative OpenMP parallel region within the worker's main loop to showcase active multi-threading.
*   **Parallel Downloading:** Enables concurrent downloading of the same torrent by multiple independent client instances (MPI workers), each contributing to the overall swarm activity.
*   **Detailed Performance Metrics:**
    *   **Instantaneous Rates:** Real-time download and upload payload rates.
    *   **Rolling Average Rates:** Calculates and displays average download/upload rates over a configurable time window (10 seconds by default) for smoother performance insights using a `RateTracker`.
    *   **Total Transferred:** Tracks total payload downloaded and uploaded in MiB.
    *   **Upload/Download Ratio:** Calculated for each worker.
*   **Input Flexibility:** Accepts `.torrent` files as input.
*   **Resume Capability:** Saves `.fastresume` data for each worker's session, allowing downloads to be potentially resumed later (though full resume logic across MPI restarts is not the primary focus).
*   **Dynamic Port Allocation:** Each MPI worker listens on a unique port to avoid conflicts.
*   **Custom User Agent:** Identifies each worker instance uniquely to peers.
*   **Final Summary Table:** The coordinator process presents a clear, formatted table summarizing the download/upload statistics and status for each worker.

## Technologies Used

*   **C++17:** Core programming language.
*   **MPI:** For process-level parallelism and inter-process communication (e.g., Open MPI).
*   **OpenMP:** For shared-memory multi-threading within each MPI process.
*   **libtorrent-rasterbar (v2.0.x):** High-performance C++ library for the BitTorrent protocol.

## Prerequisites

*   A C++17 compliant compiler (e.g., `g++`, `clang++`).
*   An MPI implementation (e.g., Open MPI, MPICH) that provides `mpicxx` and `mpirun`.
*   `libtorrent-rasterbar` development library (version 2.0.x is recommended).
    *   On Debian/Ubuntu: `sudo apt-get install libtorrent-rasterbar-dev`
    *   On macOS (Homebrew): `brew install libtorrent-rasterbar`

## Building

1.  **Clone the repository** (if applicable).
2.  **Navigate to the project root directory.**
3.  **(Optional)** Clean previous builds:
    ```bash
    make clean
    ```
4.  **Compile the project:**
    ```bash
    make
    ```
    This command will use `mpicxx` and the provided `Makefile` to compile `src/main.cpp`. The `Makefile` is configured to link against `libtorrent-rasterbar` (with paths potentially specific to a Homebrew installation on macOS, adjust `LDFLAGS` and `CPPFLAGS` in the `Makefile` if your libtorrent is installed elsewhere), OpenMP, and other necessary system libraries. The executable `parallel_torrent` will be created in the `bin/` directory.

## Running

Execute the client using `mpirun`. You need to specify:
*   `-np <N>`: The total number of MPI processes to launch. One process (Rank 0) will act as the coordinator, and the remaining `N-1` processes will be workers. For `N=1`, Rank 0 will perform the download itself.
*   `<torrent_input>`: The path to a `.torrent` file.

**Examples:**

*   **Run with 3 processes (1 coordinator, 2 workers) using a torrent file:**
    ```bash
    mpirun -np 3 ./bin/parallel_torrent /path/to/your/large_file.torrent
    ```

*   **Run in single-process mode (Rank 0 downloads directly):**
    ```bash
    mpirun -np 1 ./bin/parallel_torrent /path/to/your/test.torrent
    ```

## Output

*   **Console Logs:** Each MPI rank will print status messages, including session setup, torrent addition, progress updates (state, peer count, download/upload rates, total transferred), and completion status.
*   **Downloaded Files:** Each worker (Rank `X`) will save downloaded files into a directory named `rank_X_download/` created in the current working directory where the command was run. Resume data (`.fastresume` files) will also be saved within these directories.
*   **Final Summary:** Rank 0 (Coordinator) will print a table summarizing the total download/upload, ratio, and status for each worker once all workers have reported completion or timeout.

