# 源码与素材说明

本仓库公开宝宝英语（BaoBao English）的应用源码、构建工具和当前版本运行所需资源，便于查看与制作个人学习版本。项目作者尚未指定原创代码的开源许可证；公开可见不代表所有内容均采用同一种许可。

- 当前词表是自编的学龄前常见英语单词（6 个主题单元、60 词），不取自任何教材，可自由替换。`content/curriculum.json` 是唯一内容来源。原先配套的 72 句极简单句已从本版删除，句子内容不再随固件分发。
- 单词配图来自项目方提供的一张插画表 `assets/art/words-sheet.webp`（10×6 共 60 格），由 `tools/prepare_word_art.py` 按网格裁成 60 张 112×112 RGB565，源图与产物的 SHA256 见 `assets/art/words/manifest.json`。这张插画表不是本仓库自绘，如需再次分发请示明其授权。
- 首页背景来自项目方提供的插画，经 `tools/prepare_home_art.py` 确定性缩放到 240×320 并转成 RGB565；原文与哈希见 `assets/art/home/manifest.json`。首页插画同样不是本仓库自绘，如需再次分发请示明其授权。
- 英语朗读由 macOS 内置 `say`（Samantha，120 词/分）在本地合成，经 Opus 编码后随固件分发，见 `tools/say_audio.py`。本版只有 60 段单词朗读，全部打进一个内嵌的 `assets/audio/voice.bin`。这是合成音，不是出版社或真人录音。
- 字体许可见 `assets/fonts/FONT-NOTICES.txt`，组件许可见 `licenses/`；这些许可分别适用于对应组件，不覆盖首页插画。

替换词表或配图时，请使用自己有权使用的内容，并保留相应来源和许可说明。

历史说明：v0.5.2 及更早的版本使用科学普及出版社《英语》七年级上册（2024 年版）的教材文字与扫描插图，原始 PDF 从未随仓库分发。从 v0.5.3 起，这些教材版资源（`main/textbook_art.c`、`assets/textbook/`、`content/full-book/`）已从仓库移除——它们不属于本版内容，也不再参与构建。退役的小熊吉祥物资源（`main/bear.c`、`assets/art/bear-atlas-v0.2.0.png`、`tools/prepare_art.py`）同样已移除，v0.5.3 起所有页面都不再使用该角色。本版公开分发的只有自编词表、项目方提供的单词插画表与首页插画，以及本机合成的语音。
