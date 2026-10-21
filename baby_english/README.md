# 宝宝英语 · BaoBao English v0.5.3

面向学龄前儿童的离线英语单词点读固件，跑在 ESP32-C3 上。6 个主题单元共 **60 个常见单词**，每个词配一张插画（苹果、鸭子、时钟……）和一段英语朗读。全部资源内置在一个固件里，不联网。

只有四个页面：**首页 → 单词卡片**，以及从首页长按进去的 **设置 → 字体许可**。首页是一张项目方提供的插画（小女孩举着 apple 单词卡），底部一条固定提示「单击开始 · 长按设置」；单击任意一个键直接开始背单词，长按任意一个键进入设置。**全应用不再有任何 IP 吉祥物**：小熊角色和它那套眨眼/跟读/庆祝动画在 v0.5.3 里整体退役、相关资产已从仓库移除，每一页只有一张静态图。

许可范围见 [SOURCE-NOTICE.md](SOURCE-NOTICE.md)，替换成自己的内容见 [替换说明](content/import-guide.md)。

## 内容

| 单元 | 单词 |
|---|---|
| 1 水果 | apple banana orange pear grape peach lemon tomato mango cherry |
| 2 动物 | duck cat dog pig cow bird fish rabbit bear frog |
| 3 颜色 | red blue yellow green black white pink brown purple gray |
| 4 数字 | one two three four five six seven eight nine ten |
| 5 身体 | eye ear nose mouth hand foot face hair tooth arm |
| 6 日常用品 | book ball cup bed hat shoe bag spoon key clock |

- 每个单词都带一条通用美式音标，显示在词卡的英文下方（`apple` → `/ˈæpəl/`）。音标单独存在 `curriculum.json` 的顶层 `ipa` 映射里，**不参与** `items[]` 和两个版本号的计算——改音标既不会清空学习进度，也不会让已经合成好的语音作废。
- **没有句子、没有课程。** 单元（`units[]`）只作为词序分组留在 `curriculum.json` 里给配图脚本和测试用，界面上不再出现——首页点一下就直接出图，不再经过分类页。
- `content/curriculum.json` 是唯一内容来源；单词、音标、配图和音频 id 的顺序都由它推导，改一处即可整体重建。每个单元只有 `title_zh` 和 `words` 两个字段。
- `content/words.md` 是编译时一并生成的单词表（序号 / 单词 / 音标 / 中文 / 配图文件名），同时充当插画表的行优先清单；它由脚本重写，不要手工编辑。
- 单词配图来自项目方提供的一张 10×6 插画表（原始文件 `assets/art/words-sheet.webp`），`tools/prepare_word_art.py` 从 alpha 通道自动认出网格，逐格裁到插画本身再等比缩到 **112×112** RGB565；源图与产物哈希见 `assets/art/words/manifest.json`。格子按行优先排列，顺序就是 `curriculum.json` 的词序。
- 插画表是一张**贴纸图**：每个图案都画在透明底上，边缘自带一圈同色柔光往外淡出。所以**裁切和背景处理都由 alpha 决定**——按 alpha 取紧致外接框（阈值 8，柔光的尾巴也留下，避免在淡出中途硬切一道边），再把整张图连同透明部分一起合成到页面背景色 `#F7F3E7`（也就是 `main/ui.c` 给每一页刷的那个颜色）。这样在设备上看起来和真透明一致，而固件仍是每张 25,088 字节的 RGB565——真做逐像素 alpha 需要 RGB565A8，60 张要多占 752,640 字节。用颜色判断主体在这张表上不成立：白色颜料块、兔子的身体和页面底色几乎同色，黑色和灰色块又和任何阴影同色。
- 首页底图来自项目方提供的插画，`tools/prepare_home_art.py` 只做确定性缩放（1086×1448 → 240×320，LANCZOS）和 RGB565 转换；源图比例不是 0.75 会直接报错要求先裁图。设置页那个喇叭图标由 `tools/prepare_ui_art.py` 手写生成（16×16 ASCII 放大 4 倍），是界面固定装饰，数量与词表无关。
- 朗读由 macOS 内置 `say`（Samantha，120 词/分）本机合成，`tools/say_audio.py` 负责裁剪静音、Opus 编码和打包。**是合成音，不是真人或出版社录音。**
- 60 个单词各一段朗读，共 **60 段**，全部打进一个 `assets/audio/voice.bin`。

## 操作

- 三个实体键在设备右侧竖排，从上到下依次记为 **A（上）/ B（中）/ C（下）**，和屏幕上的按钮一一对应。设置页和字体许可页是列表：A 往前一项、B 往后一项、C 确认当前项。
- **首页**：插画加底部固定提示「单击开始 · 长按设置」。单击 A、B、C **任意一个键**直接开始，随机出一张卡片；任意一个键**按住 1 秒**进入设置页。
- **单词卡片**：顶部一行是进度（`背单词 3 / 60`，洗牌后已经看到第几张）；112×112 的图占上半屏，下面依次是英文、音标、中文释义——**中文一直显示**，不需要翻面。底部三个小按钮横排，每个按钮上写的就是要按的那个键：**`A 返回` / `B 下一个` / `C 发声`**。
  - **A 返回**：回首页。再进来会重新洗一次牌。
  - **B 下一个**：翻到下一张。顺序是**随机且不重复**的——每次开始都把 60 个词洗一遍，走完 60 张才重新洗牌，所以一个词不会在其它词都出现之前重复。换一批时会避开刚看过的那个词。
  - **C 发声**：读一遍这个单词。只出声，不改变焦点、不翻页。
  - 卡片页上**没有需要先选中的焦点**：三个按钮就是三个键，按哪个做哪件事。
- **默认不出声**：进卡片是安静的，只画图不朗读；孩子想光看图就按「下一个」一直翻。
- **设置**：四项——`减小音量并试听` / `增大音量并试听` / `查看字体许可` / `返回首页`，A / B 移动、C 确认。改音量会立刻试听一段并把新音量存档。这里没有「我会了 %u / %u 个词」那行字了，因为学习进度不再是这台机器的功能。
- **字体许可**：46 页，`上一页` / `下一页` / `返回`，A / B 移动、C 确认；返回落到设置页。
- 长按在卡片、设置、许可页上**不做任何事**（只算一次活动），避免孩子按住不放时误跳页。
- 音量默认 80%，调整后保存。闲置 60 秒熄屏，第一下按键只唤醒、不触发任何操作；播放期间不熄屏。

## 构建

需要 ESP-IDF 5.5.3；`BABY_IDF_PATH` 和 `IDF_PYTHON_ENV_PATH` 可覆盖本机路径。

```sh
# 单版本：单词音频内嵌在应用里，只出一个 BIN
bash tools/build.sh

# 宿主机测试
cmake -S tests -B build-host && cmake --build build-host -j 4
ctest --test-dir build-host --output-on-failure
```

只有一个构建版本。句子包和它依赖的 `voices` 分区已经整体下线，社区版与默认版的区别不存在了。

改内容后重新生成的顺序：`compile_content.py` → `prepare_word_art.py` → `say_audio.py` → `generate_fonts.py` → `compile_notices.py`。首页底图和界面图标与词表无关，只在换图时跑 `prepare_home_art.py` / `prepare_ui_art.py` 重跑一次即可。音标也走这条线：`compile_content.py` 会把它编进 `main/content.c` 的 `word_ipa[]`，`generate_fonts.py` 再从源码字面量里收字形，所以**加了新音标字符就必须重跑字库**，否则宿主机测试会以 `MISSING GLYPH` 直接退出。

实测体积：

| 项目 | 字节 | 说明 |
|---|---|---|
| 应用 `build/baby_english.bin` | 2,559,904 B | 0x270FA0 |
| `factory` 分区 | 3,407,872 B | 0x340000 |
| **余量** | **847,968 B（25%）** | 0xCF060 |

分区表（`partitions.csv`）只剩四项：`nvs 0x9000/0x6000`、`phy_init 0xf000/0x1000`、`factory 0x10000/0x340000`、`cardid 0x356000/0x4000`。**`cardid` 是设备身份区，任何安装动作都不碰它**（见 `docs/device-checklist.md`）。

静态数据里最大的一块是 60 张单词图：**1,505,280 B**（60 × 112×112 RGB565），比 64×64 时代多了 1,013,760 B。首页底图占 153,600 B（240×320 RGB565，全幅铺满）；单词音频包 60 段共 94,502 B，内嵌在应用里，总时长 30.0 秒。运行期 DRAM 用 122,656 B / 321,296 B（38.18%）。

## 从旧版升级

**存档格式一个字都没改。** `APP_SAVE_SIZE` 仍是 76 字节（`16 + WORD_COUNT`），magic 仍是 `BE04`，`WORD_CONTENT_VERSION` 仍由 60 个词对算出，仍是 **628472539**。所以设备上现有的存档在这一版升级后**继续有效**：音量不会被打回 80%，学习进度不会归零。`mastery[]` 这个字段留着了但永远是 0——它只为把 blob 撑到同样的长度，好让 `app_load()` 的长度与校验判据原封不动。

变成的是 `CONTENT_VERSION`（它哈希整张 `items[]` 表，句子行没了），从旧值变成 **3092071670**，所以单词音频包必须按新版本重新打包——60 段音频内容一模一样，只是包头里的版本号换了。固件会拒绝版本不符的包，因此**忘跑 `tools/say_audio.py` 就会让每张卡片都报「声音暂不可用」**，`tests/test_curated.py` 正是钉住这一点的。

再往前的存档（v0.5.1 的 `BE04` 720 词版本）仍被**直接拒绝**：旧词表是教材的 720 个词，与学龄前 60 词之间没有真实的对应关系，编一个索引映射只会把「这个学生学过 word #37」错记成另一个词。所以这里选择不作假：`curriculum.json` 里没有 `word_migration`。更早的 v0.5.0 `BE02`（151 词）格式仍然认得，但**只继承音量**，不继承那套自动熟悉度分数。

## 仓库状态

- 仓库里只有当前 v0.5.3 需要的东西：自编词表、项目方提供的单词插画表与首页插画、本机合成的语音、字库和构建脚本。
- v0.5.2 及更早的教材版资源（`main/textbook_art.c/.h`、`assets/textbook/`、`content/full-book/`、`content/listening-*.json`、`preview/`、`firmware/`）和退役的小熊吉祥物资产（`main/bear.c/.h`、`assets/art/bear-*.png`、`assets/art/states/`、`tools/prepare_art.py`）已在 v0.5.3 一并**移除**，不再保留历史副本。
- 本次（仍是 0.5.3）又下线了整条句子管线：`assets/audio-full/`（句子音频包）、`partitions.community.csv`、`sdkconfig.community.defaults`、`tools/build_community.sh`、`build-community/` 目录，以及界面上的菜单页、单元页、句子页、生词本页。仓库不是 git 仓库，所以这两次清理都是单向的。
- `tools/` 只剩当前管线的 **9 个脚本**：构建（`build.sh`）、内容与美术（`compile_content.py`、`prepare_word_art.py`、`prepare_ui_art.py`、`prepare_home_art.py`）、语音（`say_audio.py`）、字库与声明（`generate_fonts.py`、`compile_notices.py`）、预览（`preview.py`）。旧教材发布管线的 43 个脚本（教材提取、Qwen 语音合成、旧打包与发布）已随教材版一起删除——它们只互相引用，整条线一起下线。`docs/` 同样只留当前版本的文档。
- v0.5.3 起全项目统一改名成 `baby_english`：仓库目录、IDF 工程名（`CMakeLists.txt:5`）、输出固件 `build/baby_english.bin`、NVS 命名空间（`main/main.c`）、日志 tag，以及 `BABY_IDF_PATH` 这个开关（社区版开关 `BABY_COMMUNITY_WEB` 已随构建合并删除）。设备上显示的许可页标题是 `BaoBao English 0.5.3`。
- 词表里那个 `bear`（动物单元的「熊」）是教学内容，不是标识，当然不改。
- `licenses/` 里是 7 份第三方许可原文（ESP-IDF、6 个 Espressif / LVGL 组件），`SOURCE-NOTICE.md` 第 9 行明确指出组件许可见该目录，设备上的「查看字体许可」页也是从这里编出来的——**不是可以删的残留**。`managed_components/` 是 IDF 组件管理器按 `dependencies.lock` 拉下来的依赖源码，已列入 `.gitignore`，删掉会让下一次构建失败，重建也只会重新下回来。
- `device-backups/cardid-before.bin` 是设备身份区（`0x356000`，16,384 B）的刷写前快照，安装前后都要与它逐字节比对；该目录已列入 `.gitignore`，只存在于本机。
- 本版目前需要从源码构建：仓库里没有预编译固件，也没有生成过发布包。

## 验证

宿主机测试 7 项全部通过：core / input / voice / opus / word_migration / native_ui / curated_content。`input` 覆盖 30 ms 消抖、松手才判定、长按 1 秒只发一次且松手不补发、熄屏唤醒吃掉整个手势、开机时按住必须先松手。`core` 覆盖首页任意键出牌、长按进设置、洗牌是排列且两轮不同且不会开在刚离开的那张上、卡片三按钮、返回首页、熄屏与存档往返。`native_ui` 会把每一页渲染出来，对**全部 60 张卡片、设置页四个焦点、许可页三个焦点**逐个标签检查不越界、不重叠、不缺字形——音标行也在其中，五个字号都覆盖了 36 个音标字符。`curated_content` 盯着音标映射、「音标不进版本号」这条边界，以及**存档版本号仍是 628472539**。

音频包用仓库自带的宿主机校验器验过（不需要设备）：clip 数、首 ID、内容版本、逐 clip CRC 与帧完整性全部通过，版本不符或截断的包会被拒绝。UI 联络表见 `docs/evidence/ui-contact-sheet.png`（由 `tools/preview.py` 生成）。

本版已刷入实机并确认能正常启动（`Validated 60 local voice clips at 16000 Hz`，无 W/E，初始化后堆余量 180,304 B / 最大连续块 114,688 B），刷写前后身份区逐字节一致；逐条实机体验见 `docs/device-checklist.md`。仓库里没有预编译固件，也没有生成过发布包，实际刷写以设备安装记录为准。
