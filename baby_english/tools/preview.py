"""Build the UI contact sheet from host screenshots.

`build-host/test_ui <dir>` writes one 240x320 PPM per card. This picks the cards
worth a human look -- above all the word page, where the pixel picture has to
read as the word next to it -- and lays them out with Chinese captions.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

r = Path(__file__).resolve().parents[1]
p = Path(sys.argv[1]) if len(sys.argv) > 1 else r / 'build-host/screens'
for f in p.glob('*.ppm'):
    Image.open(f).save(f.with_suffix('.png'))

# There are four pages: the picture, a card, settings and the licence. test_ui
# validates every one of the 60 cards and keeps a raster for every tenth word, so
# these tiles are the first word of each unit plus the last.
TILES = [
    ('01-home', '首页（插画 + 单击开始 · 长按设置）'),
    ('02-word-default', '卡片（A 返回 · B 下一个 · C 发声）'),
    ('word-00-card', 'apple 苹果'),
    ('word-10-card', 'duck 鸭子'),
    ('word-20-card', 'red 红色'),
    ('word-30-card', 'one 一'),
    ('word-40-card', 'eye 眼睛'),
    ('word-50-card', 'book 书'),
    ('word-59-card', 'clock 时钟'),
    ('03-settings-0', '设置 · 减小音量并试听'),
    ('03-settings-3', '设置 · 返回首页'),
    ('04-license-00', '字体许可 1 / 46'),
    ('04-license-first', '许可 · 上一页'),
    ('04-license-next', '许可 · 下一页'),
]
COLS, PITCH_X, PITCH_Y = 4, 380, 380
W = 20 * 2 + PITCH_X * (COLS - 1) + 240
H = 12 + PITCH_Y * ((len(TILES) + COLS - 1) // COLS - 1) + 32 + 320 + 12
canvas = Image.new('RGB', (W, H), '#E4DFD1')
d = ImageDraw.Draw(canvas)
font = ImageFont.truetype(str(r / 'assets/fonts/LXGWWenKai-Medium.ttf'), 20)
for i, (stem, title) in enumerate(TILES):
    x, y = 20 + i % COLS * PITCH_X, 12 + i // COLS * PITCH_Y
    d.text((x, y), title, font=font, fill='#194E40')
    canvas.paste(Image.open(p / (stem + '.png')), (x, y + 32))
(r / 'docs/evidence').mkdir(parents=True, exist_ok=True)
canvas.save(r / 'docs/evidence/ui-contact-sheet.png')
print('wrote ui-contact-sheet.png (%dx%d)' % (W, H))
