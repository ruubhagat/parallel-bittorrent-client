// src/mpi_hello.cpp
#include <iostream>
#include <mpi.h> // Include the MPI header

int main(int argc, char *argv[]) {
    // Initialize the MPI environment
    // MPI_Init takes pointers to argc and argv, allowing it to process MPI-specific command-line arguments.
    MPI_Init(&argc, &argv);

    int world_size;
    // Get the total number of processes in the communicator MPI_COMM_WORLD
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int world_rank;
    // Get the rank (unique ID, 0 to world_size-1) of the current process
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    // Get the name of the processor (hostname) this process is running on
    MPI_Get_processor_name(processor_name, &name_len);

    // Each process prints its rank, the total size, and its hostname
    std::cout << "Hello from MPI process " << world_rank << " of " << world_size
              << " on processor " << processor_name << std::endl;

    // Finalize the MPI environment. No MPI calls should be made after this.
    MPI_Finalize();

    return 0;
}