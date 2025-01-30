cmake_minimum_required(VERSION 3.20)
project(webgpu-shallow-water)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(SDL2 REQUIRED)
find_package(wgpu-native REQUIRED)

# ImGui doesn't provide CMakeLists.txt...
file(GLOB IMGUI_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_sdl2.*"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_wgpu.*"
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)
target_compile_definitions(imgui PUBLIC
	-DIMGUI_IMPL_WEBGPU_BACKEND_WGPU=1
)
target_link_libraries(imgui PUBLIC
	wgpu-native
	SDL2::SDL2
)

file(GLOB_RECURSE SOURCES
    LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*"
)

set(TARGET_NAME "${PROJECT_NAME}")

add_executable(${TARGET_NAME} ${SOURCES})
target_include_directories(${TARGET_NAME} PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)
target_link_libraries(${TARGET_NAME} PUBLIC
	imgui
	wgpu-native
	SDL2::SDL2
)
