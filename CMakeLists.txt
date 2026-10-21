cmake_minimum_required(VERSION 3.24)
project(lmdraw VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(LMDRAW_FULL_STATIC "Link platform C/C++ runtimes statically where supported" ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LUNASVG_DISABLE_EXTERNAL_RESOURCES ON CACHE BOOL "" FORCE)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/lunasvg lunasvg-build)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed-resources.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/schema/lmdraw.dsl.json"
          "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf"
          "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSans-Bold.ttf"
          "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp"
  DEPENDS schema/lmdraw.dsl.json assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Bold.ttf scripts/embed-resources.py
  VERBATIM)
add_executable(lmdraw cpp/main.cpp cpp/luna_renderer.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp")
target_include_directories(lmdraw PRIVATE cpp sdk "${CMAKE_CURRENT_BINARY_DIR}/generated" third_party/lunasvg/include)
target_link_libraries(lmdraw PRIVATE lunasvg::lunasvg plutovg::plutovg)
target_compile_definitions(lmdraw PRIVATE LMDRAW_VERSION="${PROJECT_VERSION}")
target_compile_options(lmdraw PRIVATE -Wall -Wextra -Wpedantic)
if(WIN32 AND MINGW)
  target_link_options(lmdraw PRIVATE -static -static-libstdc++ -static-libgcc)
elseif(UNIX AND NOT APPLE AND LMDRAW_FULL_STATIC)
  target_link_options(lmdraw PRIVATE -static -static-libstdc++ -static-libgcc)
elseif(UNIX AND NOT APPLE)
  target_link_options(lmdraw PRIVATE -static-libstdc++ -static-libgcc)
endif()
add_executable(lmdraw-unit tests/unit.cpp)
target_include_directories(lmdraw-unit PRIVATE third_party/lunasvg/include third_party/lunasvg/plutovg/include)
target_link_libraries(lmdraw-unit PRIVATE lunasvg::lunasvg plutovg::plutovg)
enable_testing()
add_test(NAME lmdraw-unit COMMAND lmdraw-unit)
