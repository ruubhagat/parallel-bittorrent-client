// src/omp_hello.cpp
#include <iostream>
#include <omp.h> // Include the OpenMP header

int main() {
    // This environment variable can control the number of threads OpenMP uses.
    // You can also set it in your shell: export OMP_NUM_THREADS=4
    // omp_get_max_threads() returns the max number of threads available.

    // Start a parallel region. The code inside will be executed by multiple threads.
    #pragma omp parallel
    {
        // Each thread executes this block of code independently.
        int thread_id = omp_get_thread_num(); // Get the unique ID (0 to num_threads-1) of the current thread
        int num_threads = omp_get_num_threads(); // Get the total number of threads in this parallel region

        // Use a critical section to prevent garbled output when multiple threads print simultaneously.
        // Only one thread can be inside a critical section with the same name at a time.
        #pragma omp critical
        {
            std::cout << "Hello from OpenMP thread " << thread_id << " of " << num_threads << std::endl;
        }
    } // End of the parallel region. Threads synchronize here.

    std::cout << "Parallel region finished." << std::endl;

    return 0;
}