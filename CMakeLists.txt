cmake_minimum_required(VERSION 3.14)
project(visuals)

set(CMAKE_CXX_STANDARD 17)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/external)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)

find_package(CURL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

set(SKIP_PQXX_INSTALL ON CACHE BOOL "" FORCE)
set(SKIP_PQXX_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/libpqxx)

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${THIRD_PARTY_DIR}/imgui 
    ${THIRD_PARTY_DIR}/imgui/backends
    ${SDL2_INCLUDE_DIRS}
)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)
target_compile_options(implot PRIVATE -fPIC)


set(PROJECT_SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/TelemetryData.cpp
    ${SRC_DIR}/Parser.cpp
    ${SRC_DIR}/Database.cpp
    ${SRC_DIR}/Network.cpp
    ${SRC_DIR}/UI.cpp
    ${SRC_DIR}/Map.cpp
)

add_executable(main ${PROJECT_SOURCES})

target_include_directories(main PRIVATE 
    ${SRC_DIR}
    ${THIRD_PARTY_DIR}/stb
    ${SDL2_INCLUDE_DIRS} 
    ${ZMQ_INCLUDE_DIRS} 
    ${PostgreSQL_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)


target_link_libraries(main PRIVATE 
    imgui 
    implot 
    pqxx
    ${PostgreSQL_LIBRARIES} 
    ${SDL2_LIBRARIES} 
    ${OPENGL_LIBRARIES} 
    ${GLEW_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
)