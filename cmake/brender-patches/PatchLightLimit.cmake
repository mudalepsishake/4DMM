# actorlight29: raise Source BRender's compile-time active-light table from
# the original 1995 limit of 16 to the practical 4DMM Light Lab cap of 256.
# The framework arrays and loops are already expressed in BR_MAX_LIGHTS, so
# changing the one public limit keeps the implementation internally coherent.

if(NOT DEFINED BRENDER_SOURCE_DIR OR "${BRENDER_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "actorlight29: BRENDER_SOURCE_DIR was not supplied")
endif()

set(_file "${BRENDER_SOURCE_DIR}/INC/brlimits.h")
if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "actorlight29: Source BRender INC/brlimits.h not found")
endif()

file(READ "${_file}" _src)
set(_old "#define BR_MAX_LIGHTS 16")
set(_new "#define BR_MAX_LIGHTS 256")
string(FIND "${_src}" "${_new}" _already)
if(NOT _already EQUAL -1)
    message(STATUS "actorlight29: Source BRender light limit already 256")
    return()
endif()

string(FIND "${_src}" "${_old}" _found)
if(_found EQUAL -1)
    message(FATAL_ERROR "actorlight29: BR_MAX_LIGHTS 16 anchor not found")
endif()

string(REPLACE "${_old}" "${_new}" _src "${_src}")
file(WRITE "${_file}" "${_src}")
message(STATUS "actorlight29: raised Source BRender BR_MAX_LIGHTS from 16 to 256")
