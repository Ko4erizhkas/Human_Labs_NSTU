cmake_minimum_required(VERSION 3.22)

project(Lab1
        VERSION 1.0.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(Threads)

include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v7.0.0
)
FetchContent_MakeAvailable(ftxui)

add_executable(${PROJECT_NAME}
        # src/UI.cpp
        src/main.cpp
        src/UI.cpp
        src/Simulation.cpp
        src/DataIO.cpp
)
# Папка с файлами наборов данных по умолчанию
target_compile_definitions(${PROJECT_NAME} PRIVATE DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ftxui::ftxui
        Threads::Threads
)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()