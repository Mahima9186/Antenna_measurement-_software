cmake_minimum_required(VERSION 3.16)
project(AMAS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic Qt build pipelines
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt 6 libraries
find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)

# Auto-detect target bitness and choose VISA defaults
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "32-bit compiler detected. Setting VISA paths to 32-bit WinNT directory.")
    set(VISA_INCLUDE_DIR_DEFAULT "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/Include")
    set(VISA_LIBRARY_DIR_DEFAULT "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/lib/msc")
    set(VISA_LIB_NAME "visa32")
else()
    message(STATUS "64-bit compiler detected. Setting VISA paths to 64-bit Win64 directory.")
    set(VISA_INCLUDE_DIR_DEFAULT "C:/Program Files/IVI Foundation/VISA/Win64/Include")
    set(VISA_LIBRARY_DIR_DEFAULT "C:/Program Files/IVI Foundation/VISA/Win64/Lib_x64/msc")
    set(VISA_LIB_NAME "visa64")
endif()

# Configure Keysight VISA paths (changeable via cache variables)
set(VISA_INCLUDE_DIR "${VISA_INCLUDE_DIR_DEFAULT}" CACHE PATH "Path to VISA include directory")
set(VISA_LIBRARY_DIR "${VISA_LIBRARY_DIR_DEFAULT}" CACHE PATH "Path to VISA library directory")

if(EXISTS "${VISA_INCLUDE_DIR}/visa.h")
    message(STATUS "VISA headers found at: ${VISA_INCLUDE_DIR}")
else()
    message(WARNING "visa.h not found at: ${VISA_INCLUDE_DIR}. Please set VISA_INCLUDE_DIR correctly.")
endif()

# Find the VISA library for the detected architecture
find_library(VISA_LIBRARY NAMES ${VISA_LIB_NAME} PATHS ${VISA_LIBRARY_DIR} NO_DEFAULT_PATH)

if(VISA_LIBRARY)
    message(STATUS "VISA library found at: ${VISA_LIBRARY}")
else()
    message(WARNING "${VISA_LIB_NAME} library not found in: ${VISA_LIBRARY_DIR}. Please set VISA_LIBRARY_DIR correctly.")
endif()

# Define VISA Imported Target
add_library(VISA::VISA UNKNOWN IMPORTED)
set_target_properties(VISA::VISA PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${VISA_INCLUDE_DIR}"
    IMPORTED_LOCATION "${VISA_LIBRARY}"
)

# Project include directories
include_directories(
    src
    src/core
    src/core/visa
    src/core/vna
    src/core/positioner
    src/core/log
    src/core/config
)

# Source files list for the core library and executable
set(SOURCES
    src/main.cpp
    src/Application.h
    src/Application.cpp
    # VISA and hardware drivers
    src/core/visa/VISASession.h
    src/core/visa/VISASession.cpp
    src/core/vna/IVnaDriver.h
    src/core/vna/FieldFoxDriver.h
    src/core/vna/FieldFoxDriver.cpp
    src/core/vna/MockVnaDriver.h
    src/core/vna/MockVnaDriver.cpp
    src/core/positioner/IPositionerDriver.h
    src/core/positioner/MockPositionerDriver.h
    src/core/positioner/MockPositionerDriver.cpp
    src/core/positioner/TAP3001Driver.h
    src/core/positioner/TAP3001Driver.cpp
    # Logging
    src/core/log/Logger.h
    src/core/log/Logger.cpp
)

# Main Executable
add_executable(AMAS ${SOURCES})
target_link_libraries(AMAS PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialPort
    VISA::VISA
)

# Unit Testing setup using FetchContent for GTest
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    
    # Prevent overriding MSVC/MinGW runtime library settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    set(TEST_SOURCES
        tests/test_vna_driver.cpp
        tests/test_positioner_driver.cpp
        # Sources under test
        src/core/visa/VISASession.cpp
        src/core/vna/FieldFoxDriver.cpp
        src/core/vna/MockVnaDriver.cpp
        src/core/positioner/MockPositionerDriver.cpp
        src/core/log/Logger.cpp
    )
    
    add_executable(AMAS_tests ${TEST_SOURCES})
    target_link_libraries(AMAS_tests PRIVATE
        gtest_main
        Qt6::Core
        Qt6::SerialPort
        VISA::VISA
    )
    
    find_package(Threads REQUIRED)
    target_link_libraries(AMAS_tests PRIVATE Threads::Threads)
    
    add_test(NAME AMAS_unit_tests COMMAND AMAS_tests)
endif()
