cmake_minimum_required(VERSION 3.24)
project(chaselev LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHASELEV_TSAN "Build with ThreadSanitizer" OFF)

add_library(chaselev INTERFACE)
add_library(chaselev::chaselev ALIAS chaselev)
target_include_directories(chaselev INTERFACE include)
target_compile_options(chaselev INTERFACE -Wall -Wextra -Wpedantic)

if(CHASELEV_TSAN)
  target_compile_options(chaselev INTERFACE -fsanitize=thread -g -O1)
  target_link_options(chaselev INTERFACE -fsanitize=thread)
endif()

enable_testing()
add_subdirectory(tests)
