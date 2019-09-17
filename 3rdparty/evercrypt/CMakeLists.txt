cmake_minimum_required(VERSION 3.10)

project(evercrypt)

file(GLOB_RECURSE EVERCRYPT_SRC "evercrypt-msr/evercrypt/*.[cS]")

add_library(evercrypt_evercrypt STATIC ${EVERCRYPT_SRC})
add_library(evercrypt::evercrypt ALIAS evercrypt_evercrypt)

target_include_directories(evercrypt_evercrypt PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/evercrypt-msr
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/evercrypt-msr/evercrypt/kremlin
)

