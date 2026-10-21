"""Keep the on-device notices identical to the distributed, readable license file."""
from pathlib import Path
import json,textwrap
R=Path(__file__).resolve().parents[1]
p=R/'assets/fonts/FONT-NOTICES.txt'
text=p.read_text()
start=text.index('English Pocket UI is a bitmap derivative')
text=('BaoBao English 0.5.3\n'
      'English audio: locally synthesized on macOS with the built-in say voice Samantha, slowed to 120 words per minute for young learners. '
      'These are synthesized readings, not publisher recordings. '
      'Artwork: the word pictures are cut from an illustration sheet supplied by the project owner, by tools/prepare_word_art.py; '
      'the home backdrop is supplied by the project owner too, and the speaker/book icons are drawn in tools/prepare_ui_art.py.\n\n'+text[start:])
p.write_text(text)
lines=[]
for line in text.splitlines():
    lines.extend(textwrap.wrap(line,31,replace_whitespace=False) or [''])
pages=['\n'.join(lines[i:i+10]) for i in range(0,len(lines),10)]
(R/'main/ep_notices.c').write_text('#include "ep_notices.h"\nconst char ep_font_notice_text[]='+json.dumps(text)+';\nconst char *const ep_notice_pages[]={\n'+',\n'.join(json.dumps(p) for p in pages)+'\n};\nconst unsigned ep_notice_count='+str(len(pages))+';\n')
print(len(pages),'notice pages')
