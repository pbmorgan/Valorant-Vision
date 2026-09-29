cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED ${CMAKE_TOOLCHAIN_FILE}) 
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Dynamically link the runtime libraries on Windows (MSVC)
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

# Download yaml-cpp because it breaks if used with vcpkg because of course...
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
)
FetchContent_MakeAvailable(yaml-cpp)

# Download motcpp
# Note: this is a fork of it I set up because its use of onnx runtime breaks on windows
# If you're looking at this in a future semester, it's in the onnx_backend.hpp file
set(MOTCPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    motcpp
    GIT_REPOSITORY https://github.com/KiingdomKey/motcpp
    GIT_TAG main
)
FetchContent_MakeAvailable(motcpp)
target_link_libraries(motcpp PUBLIC yaml-cpp onnxruntime::onnxruntime)


# Your executable
add_executable(Valorant-Vision
    "cpp/main.cpp"
    "cpp/videoplayer.cpp"
    "cpp/mainwindow.cpp"
    "cpp/LogBar.cpp"
    "cpp/minimap.cpp"
    "cpp/neuralnetwork.cpp"
    "cpp/Mapdetector.cpp"
    "cpp/modelwrapper.cpp"
    "cpp/ocr.cpp"
    "cpp/webScraper.cpp"
    "headers/videoplayer.h"
    "headers/minimap.h"
    "headers/mainwindow.h"
    "headers/ocr.h"
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
    motcpp::motcpp
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

# Copy tessdata folder next to the exe
add_custom_command(TARGET Valorant-Vision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/tessdata"
    "$<TARGET_FILE_DIR:Valorant-Vision>/tessdata"
)

# Copy data folder (players.txt) next to the exe so WebScraper finds it on any machine
add_custom_command(TARGET Valorant-Vision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/data"
    "$<TARGET_FILE_DIR:Valorant-Vision>/data"
)

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