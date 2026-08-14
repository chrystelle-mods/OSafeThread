cmake_minimum_required(VERSION 3.21)
message("Using toolchain file ${CMAKE_TOOLCHAIN_FILE}.")

project(
    OSafeThread
    VERSION 1.0.0
    DESCRIPTION "OStim-adjacent SKSE utility framework: makes scenes combat-safe and exposes an API to integrating mods."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

macro(set_from_environment VARIABLE)
    if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
        set(${VARIABLE} $ENV{${VARIABLE}})
    endif ()
endmacro()
set_from_environment(VCPKG_ROOT)

add_compile_definitions(SKYRIM)

########################################################################################################################
## CommonLibSSE-NG (alandtse/CommonLibVR branch ng, pinned) — built from the source submodule
########################################################################################################################
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)  # CommonLib defaults tests ON → would drag in Catch2
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)

########################################################################################################################
## Our plugin
########################################################################################################################
set(sources
    src/main.cpp
    src/Papyrus.cpp
    src/Hooks.cpp
    src/SceneManager.cpp
    src/Actors.cpp
    src/Config.cpp
    src/UI.cpp
)

add_library(${PROJECT_NAME} SHARED ${sources}
    src/PCH.h src/ThreadEventSink.h src/Papyrus.h src/Hooks.h src/Calm.h src/SceneManager.h src/Actors.h
    src/Config.h src/UI.h)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    # Vendored SKSE Menu Framework v3 consumer header (header-only, soft-links the framework DLL at
    # runtime via GetProcAddress — no import lib, no separate imgui.dll).
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern/SKSEMenuFramework>)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor)  # required to consume CommonLib headers

target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

########################################################################################################################
## Deploy into an MO2 mod folder (root == Data). Override -DOUTPUT_FOLDER=... to change.
########################################################################################################################
set(OUTPUT_FOLDER "C:/Steam/steamapps/common/Skyrim Special Edition/ModOrganizer Data/mods/OSafeThread"
    CACHE PATH "Mod folder to deploy the built DLL/PDB into")

set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
    # Deploy the Papyrus scripts (compiled Scripts/*.pex + Source/Scripts/*.psc) into the mod folder
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${OUTPUT_FOLDER}"
    VERBATIM)
