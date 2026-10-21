<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、精灵图、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 番茄钟应用字库（Focus）

`main/` 下的番茄钟用两款位图字体绘制界面：一款 20 px 的正文中文取自 Noto Sans SC Regular，另一款像素字体画那两处大数字。生成的 `.c` 文件入库，源 `.otf` 不入库。

| 文件 | 内容 | 用途与来源 |
| --- | --- | --- |
| [`fonts/focus_text_20.c`](fonts/focus_text_20.c) | 139 个字形，位图 16,093 字节，行高 30 | [`main/focus_ui.c`](../main/focus_ui.c) 绘制的全部标签、提示与状态行。2026-10-11 用 Noto Sans SC Regular 按 20 px 生成。 |
| [`fonts/focus_pixel_35.c`](fonts/focus_pixel_35.c) | 11 个字形，位图 4,643 字节，行高 35 | 两处大数字都用它：计时页主视觉下方那行倒计时，内容固定是 `%02d:%02d`；以及首页那个专注时长选择器中间的数字 `%d`。下面那张 5×7 点阵表按 ×5 放大。 |
| [`fonts/charset_focus.txt`](fonts/charset_focus.txt) | 字符集清单，一行一个 token | 20 px 字体的码位：可打印 ASCII、`U+00B7`，以及界面文案用到的汉字。 |
| [`fonts/charset_focus_pixel_35.txt`](fonts/charset_focus_pixel_35.txt) | 字符集清单，一行一个 token | 像素字体的码位：数字与冒号。没有空格——两个格式串里一个空格都不含。原来摆在首页那个数两侧的 `-` / `+` 已换成下面那两颗按钮图，于是它们成了标签两侧的图，而不是标签里的字形。 |

- **源字体**：Noto Sans SC Regular，SIL Open Font License 1.1。2026-10-11 自 `https://cdn.jsdelivr.net/gh/googlefonts/noto-cjk@main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf` 下载（8,331,336 字节）。`.otf` 不入库：请放在仓库之外，把路径传给生成脚本。此处不随仓库分发字体二进制，因此也不再附许可文件。像素字体完全不碰字体文件：它的字形就是生成脚本里手写的一张 5×7 点阵表，没有需要授权的第三方内容。
- **格式**：LVGL 9.5.0（即 `dependencies.lock` 锁定的版本）PLAIN、4 bpp、未压缩，文本编码为 UTF-8（`CONFIG_LV_TXT_ENC_UTF8`）。位图数组为 `const`，驻留 Flash；输入解码可能占用内部 RAM。
- **像素字体**：由 [`tools/gen_pixel_font.py`](../tools/gen_pixel_font.py) 从一张手写的 5×7 点阵表按 ×5 生成，像素字只有按整数倍放大才不糊。两处大数字**合用这一份字体**，而不是各配一个字号：它们本来就是同一种东西画在两个页面上，分成两份只会让"这两处该一样大"多一个会走偏的地方。数字都是 5 格宽、固定推进 6 格，所以倒计时逐秒变化时字不会左右跳；冒号是单独一张 3 格宽的点阵。行高等于点阵本身、基线为 0，于是墨迹顶正好落在行框顶 —— 界面本来就是按行框顶定位的，中间的换算全部消失。
- **转换工具**：[`tools/gen_font.py`](../tools/gen_font.py)，Python 3.9 + Pillow 11.3.0。它经 FreeType 栅格化，并在写文件前用与 LVGL 运行时同一套解码逻辑回读自己的输出，因此字节是校验过的而不是假定的。两个生成脚本共用 [`tools/lv_font_emit.py`](../tools/lv_font_emit.py)：cmap 规划、4 bpp 半字节打包、以及那道回读校验都在那里，两者只在"字形从哪来"这一处不同。未使用固定版本的 `lv_font_conv` 1.5.3：它在当前 Node.js 下会写出损坏的位图，详见 [CJK 位图字体参考](../docs/reference/shinku-chen/lvgl-cjk-font-subsets.zh_CN.md)。
- **重新生成**（设源 `.otf` 位于 `/path/to/NotoSansSC-Regular.otf`），在仓库根目录执行：

```bash
python3 tools/gen_font.py --font /path/to/NotoSansSC-Regular.otf --size 20 \
    --charset assets/fonts/charset_focus.txt \
    --name focus_text_20 --out assets/fonts/focus_text_20.c \
    --source-note 'Noto Sans SC Regular(SIL OFL-1.1),本地字体文件不入库;来源与生成命令见 assets/README.md'
python3 tools/gen_pixel_font.py --scale 5 \
    --charset assets/fonts/charset_focus_pixel_35.txt \
    --name focus_pixel_35 --out assets/fonts/focus_pixel_35.c
```

`--source-note` 的文本会原样写进生成文件的开头。重新生成时请保持一字不差，否则 diff 里出现的会是来源行变了，而不是你本来要做的字形改动。像素字体不需要来源行：它的来源就是这个脚本本身，生成文件的开头也是这么写的。

- **覆盖检查**：[`tools/check_font_coverage.py`](../tools/check_font_coverage.py) 读取 `main/focus_ui.c` 与 `main/focus_model.c` 的字符串字面量，只要有字符不在生成的 cmap 表里就失败。它已纳入 `./tools/validate.sh`；该门禁还要求 20 px 字体覆盖可打印 ASCII、像素字体覆盖数字与冒号。每款字体只被要求覆盖它真正会画的东西：让像素字体也背着可打印 ASCII，等于往 Flash 里塞几十 KB 一个字形都用不上的位图。新增界面文案必须同时补进对应清单文件并重新生成字体。
- **接入位置**：两个 `.c` 由 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 的 `target_sources()` 编进 `main` 组件，符号在 [`main/focus_fonts.h`](../main/focus_fonts.h) 里声明。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 精灵图（sprites）

应用直接绘制的显示位图与它们的可编辑源文件放在 `sprites/`。

### 番茄钟水果精灵（Focus）

番茄钟计时页的主视觉是一只随阶段倒计时被一口口吃掉的水果。每开始一个阶段随机抽六种里的一只，一只水果在一个阶段里换五次形状。图像来自为本项目画的三张网格素材图，由入库的生成脚本切成 30 张（6 种水果 × 5 个阶段）。

| 文件 | 内容 | 用途与来源 |
| --- | --- | --- |
| [`source/fruit-sheet-1.webp`](sprites/source/fruit-sheet-1.webp) | 1568 × 831，WebP RGB，无 alpha | 可编辑源图：西瓜、菠萝、番茄、胡萝卜各占一行，每行五个阶段。香蕉原来也画在这一张上的第一行，重画后已剪掉（见 `fruit-sheet-3.webp`）。剪的时候按**无损**重存，其余四种水果的精灵因此和剪之前逐字节一致。为本项目手绘；可入库分发。 |
| [`source/fruit-sheet-2.webp`](sprites/source/fruit-sheet-2.webp) | 1536 × 1024，WebP RGB，无 alpha | 可编辑源图：苹果，五个阶段按 3 + 2 排成两行。来源同上。 |
| [`source/fruit-sheet-3.webp`](sprites/source/fruit-sheet-3.webp) | 1536 × 1024，WebP RGB，无 alpha | 可编辑源图：香蕉（重画版），五个阶段一排。整只偏竖长，被吃掉后剩下的是弯下来的果皮。来源同上。 |
| `focus_fruit_<水果>.c`（6 个文件） | 30 个 RGB565 数组，像素数据共 685,802 字节 | 每种水果一个文件、五个阶段，由 [`tools/gen_sprites.py`](../tools/gen_sprites.py) 生成。经 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 的 `target_sources()` 编进 `main` 组件。 |

- **格式**：RGB565，不带 alpha。计时页底色是纯黑（[`main/focus_ui.c`](../main/focus_ui.c) 的 `COL_BG`），所以生成时就合到黑底上、丢掉 alpha 通道：每像素 2 字节而不是 3 或 4 字节，共约 0.65 MB 而不是 0.98 MB。字节序即 LVGL 原生的小端 RGB565 —— [`components/bsp/src/bsp_display_lvgl.c`](../components/bsp/src/bsp_display_lvgl.c) 交给 ST7789 走 SPI 时设了 `.swap_bytes = true`。
- **抠背景**：素材图带的是画上去的浅灰棋盘格，不是 alpha 通道。生成脚本从四条边向内做洪水填充，判据是"接近中性且够亮"，所以被果肉包住的白色高光会被保留而不是挖成洞；贴着背景的那一圈按覆盖率估 alpha，再对着白色反预乘，不留灰白描边。
- **缩放**：每种水果只用一个系数，五个阶段共用，取值让这种水果**最苛刻的那一档**正好卡进 134 × 134 的框。"每种一个系数"是为了保住画家画的大小关系；若每张各自撑满框，最后只剩一点的阶段会被放大到比整只还大。这个框是硬边界而不是偏好：倒计时那行字就紧贴着它的下沿。原来是 168 × 168，素材整体缩到 80% 后改成现在这个数；框的中心点没动，所以果子是在原地变小而不是挪位。[`tools/gen_sprites.py`](../tools/gen_sprites.py) 的 `BOX`、[`main/focus_fruit_math.h`](../main/focus_fruit_math.h) 的 `FOCUS_FRUIT_BOX`、[`main/focus_ui.c`](../main/focus_ui.c) 的 `FRUIT_X`/`FRUIT_Y` 三处必须一起改。
- **先生成后校验**：生成脚本把自己写出的 RGB565 解回 RGB，和源图逐像素比对，误差超过 5/6 位量化所能解释的范围就直接失败。源图留在 `source/` 以便重新生成；**不要手改** `.c` 文件。
- **重新生成**（需已安装 Pillow），在仓库根目录执行：

```bash
python3 tools/gen_sprites.py --source-dir assets/sprites/source --out-dir assets/sprites
```

- **覆盖检查**：[`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) 会把 30 个数组全部链接进来，断言一张不缺、每张都塞得进框、表的下标与 `focus_fruit_name()` 对得上、最后一档确实比第一档小。它已纳入 `./tools/validate.sh`；那里还给总字节数设了预算，重新生成后体积意外变大时会失败，而不是悄悄吃掉 Flash。

### 番茄钟首页字标（Focus）

首页标题是像素字 "Focus"，画成位图而不是用字体渲染 —— 这样字形始终是干净的像素格子，LVGL 堆里也不占任何对象。小写 `o` 不是那个字母，而是一整颗画在同一张点阵里的番茄：首页标题和计时页那只水果说的因此是同一件事。

| 文件 | 内容 | 用途与来源 |
| --- | --- | --- |
| `focus_wordmark.c` | 一个 RGB565 数组，203 × 49，像素数据 19,894 字节 | 首页标题。手写的 5 × 7 点阵字模（F、番茄、c、u、s），放大 7 倍并合到黑底，由 [`tools/gen_brand.py`](../tools/gen_brand.py) 生成。经 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 的 `target_sources()` 编进 `main` 组件。 |

- **格式**：与水果精灵完全一致 —— RGB565 合到黑底、不带 alpha。两种颜色：字形与番茄果身用 `#FF6B5A`，番茄的绿蒂用 `#57C15C`。只做整数倍放大：非整数缩放会让像素格子大小不一，一眼就不是像素风。7 倍下 29 × 7 的字模正好是 203 × 49，在 240 宽的屏幕上两侧各留 18 像素。
- **重新生成**（需已安装 Pillow），在仓库根目录执行：

```bash
python3 tools/gen_brand.py --out assets/sprites/focus_wordmark.c
```

- **覆盖检查**：上面那份精灵测试给的是 30 张水果的预算，本文件不在其中。它的 19,894 字节加进去，入库的精灵像素数据共 721,130 字节。

### 番茄钟时长选择器的加减按钮（Focus）

首页时长选择器里那个数字两侧的 `-` 和 `+` 是图，不是字。它们原来是某款字体里的两个字形，换上下面这张素材之后，那两个字形已从字库里删掉，不留一份没人渲染的位图。

| 文件 | 内容 | 用途与来源 |
| --- | --- | --- |
| [`source/picker-buttons.webp`](sprites/source/picker-buttons.webp) | 1568 × 784，WebP RGBA | 可编辑源图：一颗减号按钮、一颗加号按钮，透明底。为本项目手绘；可入库分发。 |
| `focus_buttons.c` | 两个 RGB565 数组，各 27 × 27，像素数据共 2,916 字节 | 首页时长选择器数字两侧的两颗按钮，由 [`tools/gen_buttons.py`](../tools/gen_buttons.py) 生成。经 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 的 `target_sources()` 编进 `main` 组件。 |

- **格式**：与水果精灵完全一致 —— RGB565 合到黑底、不带 alpha，小端字节序与显示链路对齐。
- **切图**：生成脚本不接受坐标。它读素材图的 alpha 通道、按阈值二值化，再找按列连通的块，所以把按钮挪到素材里别的位置也不用改命令行。找不到正好两块、或者两块宽度相差超过十六分之一，都会直接失败 —— 切歪了却没人发现的话，右边那颗按出来的可能是减号。
- **抠底**：素材的底是一层约三成不透明的白色柔光，不是干净的透明，所以阈值取得很硬（alpha ≥ 128）。半透明白光被合到黑底上，会在每条边留下一圈灰晕。
- **缩放**：先把图像对着二值化后的 alpha 预乘，再做 LANCZOS 缩放。直接缩放 RGBA 会把透明处的白色混进边缘像素，那是同一种灰晕的另一条路。素材上两块是 450 × 443 与 449 × 443，缩放时它们共用**同一个宽度和同一个高度**：各按各的宽高比算，那 1 像素的宽度差在某些尺寸上会被放大成 1 像素的高度差（34 宽时就是 33 与 34），两颗按钮就差一像素高。屏幕上它们是一对，高度是这一对的属性，所以只算一次。两块源高度真的不一致则直接失败。当前 27 宽上两块恰好都舍成 27，但那是这两个数碰巧，不是这条规则可以撤掉的理由。
- **尺寸**：27 宽是上一版 34 宽的八成，而 34 宽本身是当初 56 宽的六成。56 那版比中间那个 35px 的数字还高，整行看着像"两颗按钮夹着一个数"；把按钮缩下去，这一行才交还给数字，按钮退成数字两侧的算子。缩小按钮**不会**挪动那个数字：整行以屏幕中线对称、两颗按钮同宽，数字的横坐标只由两个间距和数字宽度决定。这一行现在是 27 + 14 + 60 + 14 + 27 = 142，240 宽的屏上两侧各留 49px。
- **按下反馈**：对应那颗键按着不放时，[`main/focus_ui.c`](../main/focus_ui.c) 用 `lv_obj_set_style_image_opa` 把那颗按钮的图压到四成不透明度。变的只有这一档透明度，没有另做一张"按下态"的位图。
- **重新生成**（需已安装 Pillow），在仓库根目录执行：

```bash
python3 tools/gen_buttons.py --out assets/sprites/focus_buttons.c
```

- **覆盖检查**：[`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) 断言两颗按钮都在、彼此同宽同高、不是空白、塞得进选择器那一行，并且尺寸对得上钉住的 27 × 27。它们的 2,916 字节加进去，入库的精灵像素数据共 724,046 字节。

### 番茄钟的场景图（Focus）

计时页那一格在一个阶段里先后画三样东西，由 [`main/focus_ui.c`](../main/focus_ui.c) 的 `focus_ui_scene()` 决定：专注进行中被一口口吃掉的水果、专注刚走完那张结束卡、以及休息中每 700 ms 轮换一帧的睡觉三帧。结束卡与三帧睡姿来自两张为本项目手绘的图。

| 文件 | 内容 | 用途与来源 |
| --- | --- | --- |
| [`source/done-card.webp`](sprites/source/done-card.webp) | 1254 × 1254，WebP RGB，无 alpha | 可编辑源图：专注结束卡 —— 闭着眼的小人、绿色对勾、四角的灰色取景框。为本项目手绘；可入库分发。 |
| [`source/sleep-frames.webp`](sprites/source/sleep-frames.webp) | 1254 × 1254，WebP RGB，无 alpha | 可编辑源图：同一个小人睡在床上，三种睡姿，嘴边的 z 一帧比一帧大。同一来源。 |
| `focus_scenes.c` | 四个 RGB565 数组，像素数据共 104,520 字节 | 一张 134 × 126 的结束卡 + 三张 88 × 134 的睡觉帧，由 [`tools/gen_scenes.py`](../tools/gen_scenes.py) 生成。经 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 的 `target_sources()` 编进 `main` 组件。 |

- **格式与抠底**：与水果精灵完全一致 —— RGB565 合到黑底、不带 alpha，小端字节序与显示链路对齐。两张素材的底也是画上去的棋盘格而不是 alpha 通道，生成脚本直接复用 [`tools/gen_sprites.py`](../tools/gen_sprites.py) 的 `background_mask()` 与 `cut_sprite()`，而不是把那套规则再写一遍，两族素材因此走的是同一次洪水填充。
- **三帧睡姿共用同一个裁切框**：三帧画的是同一张床上的同一个角色，但嘴边那个 z 一帧比一帧大，所以三帧各自的墨迹外接框都不一样（411 × 573、375 × 563、408 × 628）。各按各的外接框缩放的话，换帧时角色会忽大忽小、上下跳 —— 而"不跳"正是动画的全部意义。生成脚本改为**按床对齐**：每帧量出床的下沿、以及床最宽那一行的左端，再把三帧的墨迹范围换算到床的相对坐标里取并集，得到**一个 411 × 628 的公共裁切框**，三帧都用它裁。三帧因此都落在 88 × 134 上；写文件之前脚本还会硬断言一次三帧尺寸相同。
- **结束卡只有一张**：它没有对齐问题，外接框本身就是裁切框。四角的取景框是画面的一部分，照常切进来。
- **生成后回读校验**：与水果精灵同一道"解码回来逐像素比"的检查，另加一条硬约束 —— 结束卡那张素材必须正好切出 1 块、睡觉那张正好 3 块，所以换掉素材却没重新对裁切框会直接失败，而不是产出一份"看起来没问题"的文件。
- **重新生成**（需已安装 Pillow），在仓库根目录执行：

```bash
python3 tools/gen_scenes.py --source-dir assets/sprites/source \
    --out assets/sprites/focus_scenes.c
```

- **没有暂停，所以也没有压暗那一套**：番茄钟的一个番茄不可分割，计时中没有"停下来"这个状态（见 [`main/focus_model.h`](../main/focus_model.h) 的 `focus_run_t`），所以这一格从来不需要压暗或盖板 —— 它只在这三种画面之间切换：被吃掉的水果、专注结束卡、睡觉三帧。
- **覆盖检查**：[`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) 链接全部四个数组，断言都不空白、都塞得进共用的那个框、尺寸指纹对得上，并且断言三帧睡姿彼此同尺寸 —— 尺寸不一致在代码里就是"动画会跳"的样子。它们的 104,520 字节加进去，入库的精灵像素数据共 828,566 字节。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
