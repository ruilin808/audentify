cmake_minimum_required(VERSION 3.15)
project(AudioFingerprintingSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find FFTW3
pkg_check_modules(FFTW3 REQUIRED fftw3 fftw3f)

# Find CURL
find_package(CURL REQUIRED)

# Find TagLib for metadata extraction
pkg_check_modules(TAGLIB REQUIRED taglib)

# Try to find SQLite3 using different methods
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 sqlite3)
    endif()
endif()

# Download and build httplib and nlohmann-json automatically
include(FetchContent)

# httplib - single header HTTP library
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.1
)
FetchContent_MakeAvailable(httplib)

# nlohmann-json - JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# Find all source files automatically, but exclude main files
file(GLOB_RECURSE ALL_SOURCES 
    "src/*.cpp" 
    "src/*.c"
)

# Remove main files to avoid multiple main() definitions
set(RECOGNITION_SOURCES "")
foreach(SOURCE_FILE ${ALL_SOURCES})
    get_filename_component(FILENAME ${SOURCE_FILE} NAME)
    # Exclude any main files
    if(NOT ${FILENAME} STREQUAL "audioFingerprinting.cpp" AND 
       NOT ${FILENAME} STREQUAL "server_main.cpp" AND
       NOT ${FILENAME} STREQUAL "main.cpp")
        list(APPEND RECOGNITION_SOURCES ${SOURCE_FILE})
    endif()
endforeach()

# Find main files
set(SERVER_MAIN_PATH "")
set(CLI_MAIN_PATH "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main/server_main.cpp")
    set(SERVER_MAIN_PATH "src/main/server_main.cpp")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/server_main.cpp")
    set(SERVER_MAIN_PATH "server_main.cpp")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/server_main.cpp")
    set(SERVER_MAIN_PATH "src/server_main.cpp")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main/audioFingerprinting.cpp")
    set(CLI_MAIN_PATH "src/main/audioFingerprinting.cpp")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audioFingerprinting.cpp")
    set(CLI_MAIN_PATH "audioFingerprinting.cpp")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/audioFingerprinting.cpp")
    set(CLI_MAIN_PATH "src/audioFingerprinting.cpp")
endif()

# Get the number of recognition source files
list(LENGTH RECOGNITION_SOURCES RECOGNITION_SOURCES_COUNT)
message(STATUS "Found ${RECOGNITION_SOURCES_COUNT} recognition source files")

# Common include directories
set(COMMON_INCLUDES
    ${FFTW3_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Common libraries
set(COMMON_LIBS
    ${FFTW3_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    fftw3_threads
    pthread
)

# Add SQLite3 to common libs
if(SQLite3_FOUND)
    list(APPEND COMMON_LIBS SQLite::SQLite3)
elseif(SQLITE3_FOUND)
    list(APPEND COMMON_LIBS ${SQLITE3_LIBRARIES})
    list(APPEND COMMON_INCLUDES ${SQLITE3_INCLUDE_DIRS})
else()
    list(APPEND COMMON_LIBS sqlite3)
endif()

# Build CLI executable if found
if(CLI_MAIN_PATH)
    message(STATUS "Found CLI main at: ${CLI_MAIN_PATH}")
    add_executable(audioFingerprintingCLI
        ${CLI_MAIN_PATH}
        ${RECOGNITION_SOURCES}
    )
    
    target_include_directories(audioFingerprintingCLI PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(audioFingerprintingCLI ${COMMON_LIBS})
    target_compile_options(audioFingerprintingCLI PRIVATE 
        ${FFTW3_CFLAGS_OTHER}
        ${TAGLIB_CFLAGS_OTHER}
        -Wall -Wextra -O3
        -Wno-sign-compare
    )
    
    message(STATUS "Building CLI tool: audioFingerprintingCLI")
else()
    message(WARNING "CLI main file (audioFingerprinting.cpp) not found - skipping CLI build")
endif()

# Build Server executable if found
if(SERVER_MAIN_PATH)
    message(STATUS "Found Server main at: ${SERVER_MAIN_PATH}")
    add_executable(audioFingerprintingServer
        ${SERVER_MAIN_PATH}
        ${RECOGNITION_SOURCES}
    )
    
    target_include_directories(audioFingerprintingServer PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(audioFingerprintingServer 
        ${COMMON_LIBS}
        ${CURL_LIBRARIES}
        httplib::httplib 
        nlohmann_json::nlohmann_json
    )
    target_compile_options(audioFingerprintingServer PRIVATE 
        ${FFTW3_CFLAGS_OTHER}
        ${TAGLIB_CFLAGS_OTHER}
        -Wall -Wextra -O3
        -Wno-sign-compare
    )
    
    message(STATUS "Building HTTP Server: audioFingerprintingServer")
else()
    message(WARNING "Server main file (server_main.cpp) not found - skipping server build")
endif()

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "FFTW3 found: ${FFTW3_FOUND}")
message(STATUS "TagLib found: ${TAGLIB_FOUND}")
message(STATUS "CURL found: ${CURL_FOUND}")

# Install targets
if(CLI_MAIN_PATH)
    install(TARGETS audioFingerprintingCLI RUNTIME DESTINATION bin)
endif()
if(SERVER_MAIN_PATH)
    install(TARGETS audioFingerprintingServer RUNTIME DESTINATION bin)
endif()