# 替换自己的单词表和首页配图

当前 v0.5.3 是自编的学龄前词表：6 个主题单元、60 个常见英语单词，配图为项目方提供的插画表。词表不取自任何教材，可以自由替换。没有句子也没有课程，单元只作为词序分组存在。

替换只需要改 `content/curriculum.json` 一个文件，其余（配图、音频、字库、固件内容表）都从它推导。

1. 编辑 `content/curriculum.json`：每个单元给 `title_zh` 和 `words`（`[英文, 中文释义]` 的数组），**没有别的字段**。再加 `lessons` 会让句子占掉音频 id，把固件已经编好的每个 clip 都顶偏（`tests/test_curated.py` 的 `test_there_are_no_lessons_left` 专门拦这个）。
2. 给每个单词补读音。文件末尾的顶层 `ipa` 映射按英文单词索引，值是**不带斜杠也不带空格**的音标（词卡渲染时自己补 `/ /`）：`"apple": "ˈæpəl"`。它必须和词表**一一对应**，少一个多一个都会在 `compile_content.py` 里直接报错。
   音标单独放一张表而不是写成 `[英文, 中文, 音标]` 三元组，是为了让 `items[]` 和两个版本号（存档格式、音频包）**只由词对决定**：改音标不会清空学习进度，也不会让已经合成好的 60 段语音作废。`tests/test_curated.py` 会盯着这条边界。
3. 为新增单词补插画：单词图来自一张插画表 `assets/art/words-sheet.webp`（10 列 × 6 行，行优先排列，格子顺序就是词序）。词表变了就得重画或重排这张表——格子数不等于词数、格子对不上词序，脚本会直接报错而不是画错图。
4. 重新生成内容表与配图：
   ```sh
   python3 tools/compile_content.py
   python3 tools/prepare_word_art.py
   ```
   `prepare_word_art.py` **需要 Pillow**（要解 WebP），会用普通 `python3` 而不是 `python3 -I` 跑。它会切分插画表、输出 `assets/art/words/*.png` 和一张联络表，先看图再决定要不要重画。`compile_content.py` 还会重写 `content/words.md`——给画图的人用的词序清单，每行带序号、音标、中文和图名。
5. 重新合成语音。`tools/say_audio.py` 用 macOS 内置 `say` 逐条合成、裁静音、编码成 Opus，再全部打成一个 `assets/audio/voice.bin`（内嵌进应用，没有第二个包也没有外置分区）；每条都会检查 Opus 包帧长，任何一条不是单帧 20 ms 就报错退出，不会悄悄产出一个在设备上放不响的包。
   ```sh
   python3 tools/say_audio.py
   ```
6. 重新生成字库和设备声明——**这一步必须联网**（用 `npx lv_font_conv`），而且要在 `compile_content.py` 之后跑，否则新增的中文释义和音标没有字形，宿主机测试会以 `MISSING GLYPH` 直接退出：
   ```sh
   python3 tools/generate_fonts.py
   python3 tools/compile_notices.py
   ```
7. 跑测试、检查容量、构建：
   ```sh
   cmake -S tests -B build-host && cmake --build build-host -j 4
   ctest --test-dir build-host --output-on-failure
   bash tools/build.sh
   ```
   只有一个 `factory` 分区（3.4 MB，`0x10000` 起），没有第二个资源分区了。60 张 112×112 图固定占 1,505,280 B，是应用里最大的一块静态数据；词表加长时先算容量再动手（每多一个词约多 25 KB 图 + 1.6 KB 音频），不要为了塞下而截断单词表。

## 注意

- 音频 id 就是单词的序号，必然是 `1..WORD_COUNT` 连续，固件把内嵌包的首 clip 和总数都对一遍，对不上就拒绝出声。脚本会断言这一点，不要手工调整 id。
- 每换一次词表，**两个版本号都会变**：`WORD_CONTENT_VERSION`（存档格式）和 `CONTENT_VERSION`（音频包）。前者变更意味着设备上的存档会被拒绝、音量回到默认 80%——词表都换了，那个音量也确实没有继承的理由。后者变更意味着音频包必须重跑 `tools/say_audio.py`，否则固件会以版本不符拒绝整个包，每张卡片都报「声音暂不可用」。校验器不看你的疏忽，只看版本号。
- 换首页背景：把新图存成 `assets/art/home/home-source.png`，再跑 `python3 tools/prepare_home_art.py`。脚本要求源图比例正好是 240:320（0.75），否则报错让你先裁——塞进黑边或拉伸都会很难看。首页只显示这张图，底部压着一行固定小字「单击开始 · 长按设置」（`y=292`，12px），**没有菜单页**：单击任意键直接进卡片，长按任意键进设置。所以底图不需要为任何按钮留出安静的横向区域，整幅都可以画满；只是**最底部那条 20px 左右的横带**会被那行字盖住，别把重要画面放在那里。
- 换设置页的喇叭图标走 `tools/prepare_ui_art.py`（手写 ASCII 位图），改单词配图换 `assets/art/words-sheet.webp` 再跑 `tools/prepare_word_art.py`，改首页背景走 `tools/prepare_home_art.py`，三条流程彼此独立。
- 用自己有权使用的内容，并在 `SOURCE-NOTICE.md` 里写清来源和许可；不要沿用本仓库首页插画或历史小熊素材的授权假设。
