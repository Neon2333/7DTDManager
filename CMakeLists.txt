cmake_minimum_required(VERSION 3.21)

project(SevenDaysManager VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)

qt_standard_project_setup()

qt_add_executable(SevenDaysManager WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp

    src/app/AppConfig.h
    src/app/AppConfig.cpp
    src/app/Library.h
    src/app/Library.cpp

    src/core/BackupService.h
    src/core/BackupService.cpp
    src/core/Deduper.h
    src/core/Deduper.cpp
    src/core/FileScanner.h
    src/core/FileScanner.cpp
    src/core/GameImporter.h
    src/core/GameImporter.cpp
    src/core/GameLauncher.h
    src/core/GameLauncher.cpp
    src/core/HardLink.h
    src/core/HardLink.cpp
    src/core/HashService.h
    src/core/HashService.cpp
    src/core/LogWatcher.h
    src/core/LogWatcher.cpp
    src/core/PathUtils.h
    src/core/PathUtils.cpp
    src/core/SaveScanner.h
    src/core/SaveScanner.cpp
    src/core/TaskRunner.h
    src/core/TaskRunner.cpp
    src/core/Version.h
    src/core/Version.cpp
)

target_include_directories(SevenDaysManager PRIVATE src)

target_link_libraries(SevenDaysManager PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
)

if(MSVC)
    target_compile_options(SevenDaysManager PRIVATE /utf-8)
endif()

set_target_properties(SevenDaysManager PROPERTIES
    WIN32_EXECUTABLE TRUE
    VS_GLOBAL_ApplicationManifest "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
)

qt_add_executable(SevenDaysCoreTests
    tests/test_core.cpp
    src/core/PathUtils.h
    src/core/PathUtils.cpp
    src/core/SaveScanner.h
    src/core/SaveScanner.cpp
    src/core/Version.h
    src/core/Version.cpp
)

target_include_directories(SevenDaysCoreTests PRIVATE src)
target_link_libraries(SevenDaysCoreTests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(SevenDaysCoreTests PRIVATE /utf-8)
endif()

qt_add_executable(SevenDaysDedupTests
    tests/test_dedup.cpp
    src/core/BackupService.h
    src/core/BackupService.cpp
    src/core/Deduper.h
    src/core/Deduper.cpp
    src/core/FileScanner.h
    src/core/FileScanner.cpp
    src/core/HardLink.h
    src/core/HardLink.cpp
    src/core/HashService.h
    src/core/HashService.cpp
    src/core/PathUtils.h
    src/core/PathUtils.cpp
)

target_include_directories(SevenDaysDedupTests PRIVATE src)
target_link_libraries(SevenDaysDedupTests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(SevenDaysDedupTests PRIVATE /utf-8)
endif()

qt_add_executable(SevenDaysLogTests
    tests/test_logwatcher.cpp
    src/core/LogWatcher.h
    src/core/LogWatcher.cpp
)

target_include_directories(SevenDaysLogTests PRIVATE src)
target_link_libraries(SevenDaysLogTests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(SevenDaysLogTests PRIVATE /utf-8)
endif()
