#!/usr/bin/env python3
from pathlib import Path
import re,subprocess,json,hashlib,argparse
from PIL import ImageFont
ROOT=Path(__file__).resolve().parents[1];FONT=ROOT/'assets/fonts/LXGWWenKai-Medium.ttf'
TOKEN=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"')
def chars(include_content=True):
 text=''
 for p in (ROOT/'main').glob('*.[ch]'):
  if p.name.startswith(('ep_font','ep_art','ep_vocab_art','word_art')) or (not include_content and p.name=='content.c'):continue
  for m in TOKEN.finditer(p.read_text()):
   if m[0].startswith('"'):text+=re.sub(r'\\(?:x[0-9a-fA-F]+|[0-7]{1,3}|.)','',m[0][1:-1])
 return ''.join(sorted(set(c for c in text if ord(c)>=32 and not c.isspace())|set(map(chr,range(32,127)))))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');args=ap.parse_args();text=chars();records=[]
 # The 16px+ faces carry the Chinese UI labels, which are all string literals in
 # main/*.c and so reach `chars()` already; the word meanings only ever land on
 # the 12/14px faces, so those two sizes stay authoritative for content.c. The
 # 16px+ faces still take every non-CJK character, because the word spellings and
 # the IPA on a card are read at those sizes.
 for size in (12,14,16,20,24):
  dest=ROOT/f'main/ep_font{size}.c';subset=text if size<=14 else ''.join(sorted(set(chars(False))|set(c for c in text if not '\u4e00'<=c<='\u9fff')))
  if args.check:
   code=dest.read_text();packed=set(int(cp,16) for cp in re.findall(r'EP_CMAP U\+([A-F0-9]+)',code));assert set(map(ord,subset))<=packed,f'font {size}: missing characters';assert re.search(r'\.bpp\s*=\s*4',code)
  else:
   f=ImageFont.truetype(str(FONT),size);unknown=bytes(f.getmask(chr(0x10ffff)))
   for ch in subset:
    if not ch.isspace():assert bytes(f.getmask(ch))!=unknown,f'missing U+{ord(ch):04X}'
   subprocess.run(['npx','--yes','lv_font_conv@1.5.3','--font',str(FONT),'--size',str(size),'--bpp','4','--no-kerning','--format','lvgl','--lv-include','lvgl.h','--symbols',subset,'--output',str(dest)],check=True,stdout=subprocess.DEVNULL)
   code=dest.read_text();attribution='/* English Pocket UI bitmap derivative. Original LXGW/Klee attribution and complete SIL OFL: assets/fonts/FONT-NOTICES.txt. */\n'
   dest.write_text(attribution+'\n'.join(f'/* EP_CMAP U+{ord(ch):04X} */' for ch in subset)+'\n'+code.replace(str(ROOT)+'/',''))
  records.append({'size':size,'codepoints':list(map(ord,subset))});print(f'{size}px {len(subset)} glyphs, 4bpp',flush=True)
 if not args.check:(ROOT/'assets/fonts/manifest.json').write_text(json.dumps({'derivative_family':'English Pocket UI','source_family':'LXGW WenKai Medium','source_url':'https://github.com/lxgw/LxgwWenKai','source_sha256':hashlib.sha256(FONT.read_bytes()).hexdigest(),'bpp':4,'fonts':records},indent=2)+'\n')
if __name__=='__main__':main()
