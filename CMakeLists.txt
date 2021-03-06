cmake_minimum_required(VERSION 3.7)

set (CMAKE_CXX_STANDARD 14)

project(UniCal)

find_package (Threads)
find_package(SDL2 CONFIG REQUIRED)
find_package(CURL REQUIRED)

find_library(SDL2_IMAGE_LIBRARY SDL2_image CONFIG REQUIRED)
find_library(SDL2_IMAGE_LIB SDL2_image CONFIG REQUIRED)
find_library(SDL2_TTF_LIB SDL2_ttf CONFIG REQUIRED)

add_definitions(-D__STDC_WANT_LIB_EXT1__=1)

file(GLOB SRCS src/*.cpp)
include_directories(src)

add_executable(UniCal ${SRCS})
target_link_libraries(
  UniCal PRIVATE SDL2 ${SDL2_IMAGE_LIB} ${SDL2_TTF_LIB} ${CURL_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
