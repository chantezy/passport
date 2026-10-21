cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.5.3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(baby_english)

# esp_audio_codec 2.6.2 adds a shell-style "-L path" entry which breaks on
# workspace names containing spaces. Both prebuilt archives already use absolute
# paths, so this redundant link-search entry is safely removed after registration.
idf_component_get_property(BABY_CODEC_LIB espressif__esp_audio_codec COMPONENT_LIB)
get_target_property(BABY_CODEC_LINKS ${BABY_CODEC_LIB} LINK_LIBRARIES)
if(BABY_CODEC_LINKS)
  list(FILTER BABY_CODEC_LINKS EXCLUDE REGEX "^-L ")
  set_property(TARGET ${BABY_CODEC_LIB} PROPERTY LINK_LIBRARIES "${BABY_CODEC_LINKS}")
endif()
get_target_property(BABY_CODEC_INTERFACE ${BABY_CODEC_LIB} INTERFACE_LINK_LIBRARIES)
if(BABY_CODEC_INTERFACE)
  list(FILTER BABY_CODEC_INTERFACE EXCLUDE REGEX "^-L |LINK_ONLY:-L ")
  set_property(TARGET ${BABY_CODEC_LIB} PROPERTY INTERFACE_LINK_LIBRARIES "${BABY_CODEC_INTERFACE}")
endif()
