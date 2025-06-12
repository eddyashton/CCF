# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

# Build r3
set(R3_DIR "${CCF_3RD_PARTY_INTERNAL_DIR}/r3")
set(R3_SRC "${R3_DIR}/src")
set(R3_DEFS "TODO")

set(R3_SRCS
  "${R3_SRC}/edge.c"
  "${R3_SRC}/match_entry.c"
  "${R3_SRC}/memory.c"
  "${R3_SRC}/node.c"
  "${R3_SRC}/slug.c"
  "${R3_SRC}/str.c"
  "${R3_SRC}/token.c"
)

find_library(PCRE2_LIBRARY REQUIRED NAMES pcre2-8)

add_library(r3 STATIC ${R3_SRCS})
target_compile_definitions(r3 PRIVATE ${R3_DEFS})

target_include_directories(r3 PRIVATE "${R3_DIR}")

target_link_libraries(r3 PUBLIC ${PCRE2_LIBRARY})
set_property(TARGET r3 PROPERTY POSITION_INDEPENDENT_CODE ON)
add_san(r3)

if(CCF_DEVEL)
  install(
    TARGETS r3
    EXPORT ccf
    DESTINATION lib
  )
endif()
