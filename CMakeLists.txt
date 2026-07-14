cmake_minimum_required(VERSION 3.20)

project(ZapretControl LANGUAGES CXX RC)

add_executable(ZapretControl WIN32
    src/main.cpp
    src/TrayIcon.cpp
    src/ProcessManager.cpp
    src/SettingsWindow.cpp
    src/Config.cpp
    src/Logger.cpp
    src/StrategyScanner.cpp
    res/app.rc
)

target_compile_features(ZapretControl PRIVATE cxx_std_17)

set_target_properties(ZapretControl PROPERTIES
    OUTPUT_NAME "zapretcontrol"
)

target_include_directories(ZapretControl PRIVATE
    src
    res
)

target_compile_definitions(ZapretControl PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ZapretControl PRIVATE
    user32
    shell32
    comdlg32
    advapi32
    ole32
)


if(MSVC)
    target_link_options(ZapretControl PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()




