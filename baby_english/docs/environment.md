# 构建环境

普通固件重建只用仓库内已生成的文字、像素图、首页底图、字库和语音，不需要外部 PDF、模型或 API Key，也不需要联网。只有重新生成字库才会用到网络。

## 构建固件与跑测试

- ESP-IDF 5.5.3；默认目录为 `$HOME/esp/esp-idf-v5.5.3`，可用 `BABY_IDF_PATH` 覆盖。
- 默认 IDF Python 环境为 `$HOME/.espressif/python_env/idf5.5_py3.11_env`，可用 `IDF_PYTHON_ENV_PATH` 覆盖。本机只装了 `idf5.5_py3.9_env`，构建时按后者指定：
  ```sh
  export BABY_IDF_PATH=/path/to/esp-idf-5.5.3
  export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.5_py3.9_env
  bash tools/build.sh              # 单版本，单词音频内嵌在应用里
  ```
- 只有一个构建版本：句子包和 `voices` 分区已整体下线，`tools/build_community.sh` 随之删除，不存在「默认版 / 社区版」之分。
- IDF Component Manager 按 `dependencies.lock` 下载依赖；**全新环境下第一次构建需要联网**，之后可离线。
- 本机测试需要 CMake、C 编译器和 Python 3；先完成 IDF 依赖下载，再运行 README 中的测试命令。构建流程不连接也不刷写设备。

## 重新生成资源

| 脚本 | 依赖 |
|---|---|
| `tools/compile_content.py` | 纯 Python |
| `tools/prepare_word_art.py` | Pillow（读 WebP 插画表） |
| `tools/prepare_ui_art.py` | 纯 Python |
| `tools/prepare_home_art.py` | Pillow（读源图） |
| `tools/say_audio.py` | macOS 内置 `say` 与 `afconvert`；**不需要 ffmpeg** |
| `tools/generate_fonts.py` | Pillow + `lv_font_conv@1.5.3`（`npx` 调用，**需要联网**） |
| `tools/compile_notices.py` | 纯 Python |
| `tools/preview.py` | Pillow（只做联络表，可选） |

需要 Pillow 的脚本要用**普通 `python3`** 跑，不能用 `python3 -I`：隔离模式看不到 user site-packages，`import PIL` 会直接失败。本机是 Python 3.9.6。
