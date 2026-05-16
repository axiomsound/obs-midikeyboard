cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/plugin-support.c.in
    src/piano-source.cpp
    src/midi-input.cpp
    extern/rtmidi/RtMidi.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    extern/rtmidi
)

if(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winmm)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE __WINDOWS_MM__)
endif()

if(APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE __MACOSX_CORE__)
endif()

if(UNIX AND NOT APPLE)
    find_package(ALSA REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ALSA::ALSA)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE __LINUX_ALSA__)
endif()

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Inno Setup installer (for local builds)
find_program(ISCC iscc)
if(ISCC)
    add_custom_target(installer
        COMMAND "${ISCC}" "/dBuildConfig=$<CONFIG>" "${CMAKE_CURRENT_SOURCE_DIR}/installer/setup.iss"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/installer/obs-midikeyboard-${PROJECT_VERSION}-windows-x64.exe"
        COMMENT "Building Inno Setup installer..."
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/installer"
    )
    add_dependencies(installer ${CMAKE_PROJECT_NAME})
endif()
