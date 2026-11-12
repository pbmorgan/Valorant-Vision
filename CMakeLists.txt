cmake_minimum_required(VERSION 3.16)

# Choose the vcpkg toolchain.
# Prefer the user's standalone vcpkg installation when available.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "$ENV{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING ""
        )
    elseif(DEFINED ENV{VCPKG_ROOT})
        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING ""
        )
    endif()
endif()

# Use the standalone vcpkg installation
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

project(Valorant-Vision)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets MultimediaWidgets Multimedia REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(Leptonica CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

include(FetchContent)

if(MSVC)
    set(YAML_MSVC_SHARED_RT ON CACHE BOOL "" FORCE)
endif()

# Download yaml-cpp because it breaks if used with vcpkg because of course...
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
)
FetchContent_MakeAvailable(yaml-cpp)



# Your executable
add_executable(Valorant-Vision
    "cpp/main.cpp"
    "cpp/videoplayer.cpp"
    "cpp/mainwindow.cpp"
    "cpp/mainwindow_teams.cpp"
    "cpp/mainwindow_video.cpp"
    "cpp/LogBar.cpp"
    "cpp/minimap.cpp"
    "cpp/minimap_detection.cpp"
    "cpp/neuralnetwork.cpp"
    "cpp/Mapdetector.cpp"
    "cpp/modelwrapper.cpp"
    "cpp/ocr.cpp"
    "cpp/ocr_frame.cpp"
    "cpp/ocr_replay_spike.cpp"
    "cpp/ocr_teams.cpp"
    "cpp/ocr_scores.cpp"
    "cpp/ocr_round_timer.cpp"
    "cpp/ocr_text.cpp"
    "cpp/ocr_killfeed_matching.cpp"
    "cpp/ocr_killfeed.cpp"
    "cpp/ocr_killer_agents.cpp"
    "cpp/ocr_assists.cpp"
    "cpp/ocr_victim_agents.cpp"
    "cpp/ocr_kill_events.cpp"
    "cpp/webScraper.cpp"
    "cpp/webScraper_refresh.cpp"
    "cpp/playertracker.cpp"
    "cpp/playertracker_stats.cpp"
    "cpp/playertracker_assists.cpp"
    "cpp/playerstatswidget.cpp"
    "cpp/killfeedagentmatcher.cpp"
    "cpp/assistcandidatematcher.cpp"

    "headers/videoplayer.h"
    "headers/minimap.h"
    "headers/mainwindow.h"
    "headers/ocr.h"
    "headers/ocr_layout_internal.h"
    "headers/playertracker.h"
    "headers/playerstatswidget.h"
    "headers/killfeedagentmatcher.h"
    "headers/assistcandidatematcher.h"
)

# Lets us just include the files without having to specify relative path
target_include_directories(Valorant-Vision PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/cpp")
target_include_directories(Valorant-Vision PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/headers")

# Link Qt + OpenCV
target_link_libraries(Valorant-Vision PUBLIC
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${OpenCV_LIBS}
    onnxruntime::onnxruntime
    Eigen3::Eigen
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    Tesseract::libtesseract
    leptonica
    CURL::libcurl
)

# Copy the runtime DLLs to the output directory
add_custom_command(TARGET Valorant-Vision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:Valorant-Vision>
    $<TARGET_FILE_DIR:Valorant-Vision>
    COMMAND_EXPAND_LISTS
)

# adds the minimap folder to the .exe directory
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/map_layouts"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/map_layouts"
)

# Adds the agent icons folder to the .exe directory
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/model/agent_icons_round"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/agent_icons"
)



# Copy Killfeed Agent Templates
# Used for killer, victim, and assist detection.

add_custom_command(
    TARGET Valorant-Vision POST_BUILD

    # Existing main and assist agent templates
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/agent_templates/templates"
        "$<TARGET_FILE_DIR:Valorant-Vision>/agent_templates/templates"

    # Existing agent masks, including 24x24 assist masks
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/agent_templates/masks"
        "$<TARGET_FILE_DIR:Valorant-Vision>/agent_templates/masks"

    # NEW: Red and teal background templates
    # for the experimental assist candidate matcher
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/agent_templates/team_backgrounds"
        "$<TARGET_FILE_DIR:Valorant-Vision>/agent_templates/team_backgrounds"
)


# Copy tessdata folder next to the exe
add_custom_command(TARGET Valorant-Vision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/tessdata"
    "$<TARGET_FILE_DIR:Valorant-Vision>/tessdata"
)
# Create the runtime players.txt from the source seed database
# only when the runtime database does not already exist.
#
# This prevents rebuilding the project from overwriting teams
# that were added to players.txt while the application was running.
add_custom_command(TARGET Valorant-Vision POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_FILE=${CMAKE_SOURCE_DIR}/data/players.txt"
        "-DDEST_FILE=$<TARGET_FILE_DIR:Valorant-Vision>/data/players.txt"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopySeedData.cmake"
)


# The 159 MB detection model is intentionally excluded from source-only ZIPs.
# Its original copy is needed for full application builds.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/model/faster_rcnn.onnx")
    message(FATAL_ERROR
        "Missing model/faster_rcnn.onnx. Copy this 159 MB file from your "
        "original Valorant-Vision-clean/model directory into this project's "
        "model directory before configuring/building the application.")
endif()

# Copy over singular files
foreach(DLL
    "${CMAKE_SOURCE_DIR}/model/id_to_name.json"
    "${CMAKE_SOURCE_DIR}/model/osnet_x1_0_dukemtmcreid.onnx"
    "${CMAKE_CURRENT_SOURCE_DIR}/model/faster_rcnn.onnx"

)
    add_custom_command(TARGET Valorant-Vision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DLL}" "$<TARGET_FILE_DIR:Valorant-Vision>"
    )
endforeach()

# So we know where the root of the project is
target_compile_definitions(Valorant-Vision PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
) 

# For adding logo to the app window
qt_add_resources(APP_RESOURCES resources.qrc)
target_sources(Valorant-Vision PRIVATE ${APP_RESOURCES})
# Optional debug info
message(STATUS "Qt6_DIR: ${Qt6_DIR}")
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV included libs: ${OpenCV_LIBS}")

# ==========================================
# Standalone Assist Detection Test
# ==========================================

add_executable(assist_detection_test
    assist_detection_test/test_assist.cpp
)

target_include_directories(assist_detection_test PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(assist_detection_test PRIVATE
    ${OpenCV_LIBS}
)
target_compile_definitions(assist_detection_test PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)
# Copy required OpenCV DLLs next to test executable
add_custom_command(TARGET assist_detection_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:assist_detection_test>
        $<TARGET_FILE_DIR:assist_detection_test>
    COMMAND_EXPAND_LISTS
)


# ==========================================
# Standalone PlayerTracker Assist Test
# ==========================================

add_executable(playertracker_test
    assist_detection_test/test_playertracker.cpp
    cpp/playertracker.cpp
    cpp/playertracker_stats.cpp
    cpp/playertracker_assists.cpp
    headers/playertracker.h
)

# PlayerTracker inherits QObject and uses
# Qt signals, so Qt's Meta-Object Compiler
# must process its header.
set_target_properties(playertracker_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(playertracker_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/headers"
)

target_link_libraries(playertracker_test PRIVATE
    Qt6::Core
)




# ----------------------------------------------------------
# Live OCR -> PlayerTracker assist integration smoke tests
# ----------------------------------------------------------
add_executable(assist_pipeline_test
    assist_detection_test/assist_pipeline_test.cpp
    cpp/playertracker.cpp
    cpp/playertracker_stats.cpp
    cpp/playertracker_assists.cpp
    headers/playertracker.h
)
set_target_properties(assist_pipeline_test PROPERTIES AUTOMOC ON)
target_include_directories(assist_pipeline_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/headers"
)
target_link_libraries(assist_pipeline_test PRIVATE Qt6::Core)
