cmake_minimum_required(VERSION 3.10)

project(CLI11)

add_library(CLI11 INTERFACE)
add_library(CLI11::CLI11 ALIAS CLI11)

target_include_directories(CLI11 INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
