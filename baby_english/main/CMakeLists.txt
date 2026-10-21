file(GLOB FONTS "ep_font*.c")
idf_component_register(SRCS "main.c" "ep_core.c" "ep_input.c" "ep_audio.c" "ep_voice.c" "ep_opus.c" "content.c" "ui.c" "home_art.c" "ui_art.c" "word_art.c" "ep_notices.c" ${FONTS} INCLUDE_DIRS "." REQUIRES esp_audio_codec bsp nvs_flash esp_timer esp_system EMBED_FILES "../assets/audio/voice.bin")
