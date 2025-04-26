# Makefile

# --- Compiler ---
# Use the MPI C++ compiler wrapper. Assumes it uses underlying clang++ on macOS.
CXX = mpicxx

# --- Directories ---
SRCDIR = src
INCDIR = include
BUILDDIR = build
BINDIR = bin

# --- Compiler Flags ---
# -g: Debugging symbols
# -Wall: Enable most warnings
# -std=c++17: Use C++17 standard
# -fopenmp=libomp: Enable OpenMP support using LLVM's runtime (REQUIRED for compilation)
# $(shell pkg-config --cflags libtorrent-rasterbar): Get include paths for libtorrent
# Make sure pkg-config works or replace with explicit paths like -I/opt/homebrew/include
PKG_CONFIG_CFLAGS = $(shell pkg-config --cflags libtorrent-rasterbar)
CXXFLAGS = -g -Wall -std=c++17 -fopenmp=libomp -I$(INCDIR) $(PKG_CONFIG_CFLAGS)

# --- Linker Flags ---
# -fopenmp=libomp: Link against LLVM's OpenMP runtime (REQUIRED for linking)
# $(shell pkg-config --libs libtorrent-rasterbar): Get library paths/names for libtorrent
# -pthread: Link against the pthreads library (often required)
# Make sure pkg-config works or replace with explicit paths like -L/opt/homebrew/lib -ltorrent-rasterbar
PKG_CONFIG_LIBS = $(shell pkg-config --libs libtorrent-rasterbar)
LDFLAGS = -fopenmp=libomp $(PKG_CONFIG_LIBS) -pthread

# --- Files ---
# Define the main target executable
TARGET_EXEC = $(BINDIR)/parallel_torrent
# Find all .cpp files in SRCDIR for the main target (example)
# SOURCES = $(wildcard $(SRCDIR)/*.cpp) # Adjust as needed for your main target
SOURCES = $(SRCDIR)/main.cpp # Start with just main.cpp for now
# Create object file names in BUILDDIR based on source names
OBJECTS = $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(SOURCES))

# --- Rules ---

# Default target: build the main executable (currently needs main.cpp)
all: $(TARGET_EXEC)

# Link the main executable
$(TARGET_EXEC): $(OBJECTS) | $(BINDIR)
	@echo "Linking main target: $@"
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

# Generic pattern rule to compile source files into object files in BUILDDIR
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@echo "Compiling $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Verification Targets ---

# Target to build the MPI hello world example
MPI_HELLO_EXEC = $(BINDIR)/mpi_hello
MPI_HELLO_SRC = $(SRCDIR)/mpi_hello.cpp
MPI_HELLO_OBJ = $(BUILDDIR)/mpi_hello.o

$(MPI_HELLO_EXEC): $(MPI_HELLO_OBJ) | $(BINDIR)
	@echo "Linking MPI Test: $@"
	$(CXX) $(MPI_HELLO_OBJ) -o $@ $(LDFLAGS)

$(MPI_HELLO_OBJ): $(MPI_HELLO_SRC) | $(BUILDDIR)
	@echo "Compiling MPI Test: $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Target to build the OpenMP hello world example
OMP_HELLO_EXEC = $(BINDIR)/omp_hello
OMP_HELLO_SRC = $(SRCDIR)/omp_hello.cpp
OMP_HELLO_OBJ = $(BUILDDIR)/omp_hello.o

$(OMP_HELLO_EXEC): $(OMP_HELLO_OBJ) | $(BINDIR)
	@echo "Linking OMP Test: $@"
	$(CXX) $(OMP_HELLO_OBJ) -o $@ $(LDFLAGS) # LDFLAGS contains -fopenmp

$(OMP_HELLO_OBJ): $(OMP_HELLO_SRC) | $(BUILDDIR)
	@echo "Compiling OMP Test: $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@ # CXXFLAGS contains -fopenmp

# --- Directory Creation ---
# These rules MUST appear only ONCE
$(BINDIR) $(BUILDDIR):
	@echo "Creating directory: $@"
	@mkdir -p $@

# --- Clean Up ---
clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILDDIR) $(BINDIR)

# Phony targets don't represent files
.PHONY: all clean test_mpi test_omp

# Convenience targets (optional)
test_mpi: $(MPI_HELLO_EXEC)
test_omp: $(OMP_HELLO_EXEC)