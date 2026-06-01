# ==============================================================================
# @file CMakeLists.txt
# @brief Build configuration for the Smart Hive IoT Node.
# @details Orchestrates the compilation of the edge inference node, linking
#          OpenCV for image processing, HailoRT for NPU acceleration, and
#          standard POSIX threads.
# @version 1.0
# ==============================================================================

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Define the project name, version, and primary programming language
project(SmartHive VERSION 1.0 LANGUAGES CXX)

# --- 1. Compiler Configuration ---
# Enforce C++17 standard compliance
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Disable compiler-specific extensions (e.g., gnu++17) to ensure portability
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 2. External Dependencies ---
# Locate OpenCV framework (Required for image processing and matrix operations)
find_package(OpenCV REQUIRED)

# Locate HailoRT library (Required for AI hardware acceleration)
# Assumes standard installation paths provided by the Hailo .deb package
find_library(HAILORT_LIB hailort REQUIRED)

# --- 3. Internal Directories ---
# Include local project header files located in the /include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- 4. Build Targets ---
# Define the main executable and define the source files required to build it
add_executable(smart_hive_node
    src/main.cpp
    src/hailo_inference.cpp
    src/rpicam_pipe.cpp
    src/lora_serial.cpp
    src/object_tracker.cpp
    src/bme280_linux.cpp
    src/system_monitor.cpp
)

# --- 5. Linker Configuration ---
# Link the required shared libraries to the target executable
target_link_libraries(smart_hive_node
    PRIVATE
    ${OpenCV_LIBS}
    ${HAILORT_LIB}
    pthread         
)