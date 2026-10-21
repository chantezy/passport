idf_component_register(
    SRCS "main.c"
         "focus_app.c"
         "focus_model.c"
         "focus_ui.c"
         "focus_sprite_view.c"
         "focus_fruit_math.c"
         "focus_sprites.c"
         "focus_store.c"
         "focus_chime.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 界面自带的两份位图字体:focus_text_20 由 tools/gen_font.py 从 Noto Sans SC 生成,
# focus_pixel_35 由 tools/gen_pixel_font.py 从一个手写的 5×7 点阵生成。都在
# assets/fonts/,不在本组件目录下,所以不能用 SRCS 登记。字体来源、许可与完整
# 生成命令见 assets/README.md。lvgl.h 与 focus_fonts.h 都来自本组件已有的包含路径。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/focus_text_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/focus_pixel_35.c"
)

# 计时页那一格上"水果之外"的画面:专注结束卡与休息中轮换的睡觉三帧,由
# tools/gen_scenes.py 从 assets/sprites/source/ 下的两张素材图切出来。和水果精灵
# 同类:纯数据、不引用 LVGL,主机测试可以直接编进来断言三帧尺寸一致(动画不会跳)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_scenes.c"
)

# 六种水果 × 五个阶段的 RGB565 位图,同样由 tools/gen_sprites.py 生成在
# assets/sprites/。它们是纯数据、不引用 LVGL,所以主机测试也能直接编进来断言尺寸
# (tests/test_focus_sprites.c,见 tools/validate.sh)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_banana.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_watermelon.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_pineapple.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_tomato.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_carrot.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_fruit_apple.c"
)

# 首页标题的像素字 "Focus"(tools/gen_brand.py 生成)与时长选择器的两颗按钮
# (tools/gen_buttons.py 生成)。和水果精灵同类:纯数据、不引用 LVGL,形状复用
# focus_sprites.h 的 focus_sprite_t。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_wordmark.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/sprites/focus_buttons.c"
)
