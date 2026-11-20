# Optional pinned BSD backend; no network activity in a default build.
set(BF_SPEEXDSP_SOURCE_DIR "" CACHE PATH "Existing SpeexDSP 1.2.1 source tree (otherwise fetch pinned commit)")
if(NOT BF_SPEEXDSP_SOURCE_DIR)
  include(FetchContent)
  FetchContent_Declare(baffle_speex_source GIT_REPOSITORY https://github.com/xiph/speexdsp.git
    GIT_TAG 1b28a0f61bc31162979e1f26f3981fc3637095c8)
  FetchContent_GetProperties(baffle_speex_source)
  if(NOT baffle_speex_source_POPULATED)
    FetchContent_Populate(baffle_speex_source)
  endif()
  set(BF_SPEEXDSP_SOURCE_DIR "${baffle_speex_source_SOURCE_DIR}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SpeexDSP-source-notices.txt"
  "Notices from the reviewed SpeexDSP 1.2.1 source subset.\nSources remain unmodified; Baffle renames symbols, supplies private hooks and configures the capture DC-notch radius through src/speex/mdf_baffle.c.\n\n")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpeexPins.cmake)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/speex-symbols.txt" BF_SPEEX_SYMBOLS)
foreach(symbol IN LISTS BF_SPEEX_SYMBOLS)
  target_compile_definitions(baffle PRIVATE "${symbol}=baffle_${symbol}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/speex-config")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/speex-config/speexdsp_config_types.h"
  "#include <stdint.h>\ntypedef int16_t spx_int16_t; typedef uint16_t spx_uint16_t; typedef int32_t spx_int32_t; typedef uint32_t spx_uint32_t;\n")
add_library(baffle_speex_kernel OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/speex/mdf_baffle.c"
  "${BF_SPEEXDSP_SOURCE_DIR}/libspeexdsp/fftwrap.c"
  "${BF_SPEEXDSP_SOURCE_DIR}/libspeexdsp/smallft.c")
set_target_properties(baffle_speex_kernel PROPERTIES C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
target_compile_features(baffle_speex_kernel PRIVATE c_std_11)
target_compile_definitions(baffle_speex_kernel PRIVATE FLOATING_POINT USE_SMALLFT OS_SUPPORT_CUSTOM RELEASE _USE_MATH_DEFINES "EXPORT=")
foreach(symbol IN LISTS BF_SPEEX_SYMBOLS)
  target_compile_definitions(baffle_speex_kernel PRIVATE "${symbol}=baffle_${symbol}")
endforeach()
target_include_directories(baffle_speex_kernel PRIVATE "${BF_SPEEXDSP_SOURCE_DIR}/include"
  "${BF_SPEEXDSP_SOURCE_DIR}/libspeexdsp"
  "${CMAKE_CURRENT_BINARY_DIR}/speex-config" "${CMAKE_CURRENT_SOURCE_DIR}/src/speex")
target_sources(baffle PRIVATE $<TARGET_OBJECTS:baffle_speex_kernel>)
target_include_directories(baffle PRIVATE "${BF_SPEEXDSP_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/speex-config")
if(BF_SANITIZER)
  target_compile_options(baffle_speex_kernel PRIVATE ${BF_SAN_FLAGS})
endif()
install(FILES "${BF_SPEEXDSP_SOURCE_DIR}/COPYING" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/Baffle RENAME SpeexDSP-COPYING)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SpeexDSP-source-notices.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/Baffle)
