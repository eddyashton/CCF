project(secp256k1)

add_library(secp256k1 INTERFACE)
add_library(secp256k1::secp256k1 ALIAS secp256k1)

target_include_directories(secp256k1 INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/include
)
