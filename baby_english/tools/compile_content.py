"""Validate and compile the word list; never silently truncate."""
from pathlib import Path
import argparse,json,hashlib,zlib
R=Path(__file__).resolve().parents[1]
def validate(c):
 units=c.get('units');assert isinstance(units,list) and 1<=len(units)<=32,'Expected 1..32 units'
 for u in units:
  assert isinstance(u.get('title_zh'),str) and u['title_zh'].strip()
  assert u.get('words'),'Each unit needs words'
  for en,zh in u['words']:
   assert isinstance(en,str) and en.strip() and isinstance(zh,str) and zh.strip()
 w=sum(len(u['words']) for u in units)
 assert w<=4096,'Library exceeds the indexed limit; no content was dropped'
 # The transcriptions live in their own map rather than inside the word pairs, so
 # that items[] -- and with it the audio pack version and the save format -- stays
 # byte-identical when only a pronunciation is edited.
 names=[en for u in units for en,zh in u['words']];ipa=c.get('ipa')
 assert isinstance(ipa,dict),'Expected a top-level "ipa" map keyed by the English word'
 assert set(ipa)==set(names),'ipa map must cover exactly the word list; missing %s, extra %s'%(sorted(set(names)-set(ipa)),sorted(set(ipa)-set(names)))
 for en in names:
  v=ipa[en];assert isinstance(v,str) and v.strip() and '/' not in v and not any(ch.isspace() for ch in v),'ipa for %r must be a bare transcription without slashes or spaces'%en
 if c.get('complete'):
  evidence=c.get('coverage',{});assert evidence.get('provided_source_sha256') and evidence.get('pages_verified') and evidence.get('vocabulary_verified'),'Complete status requires provided-source hash and verified page/vocabulary coverage'
 return w

def compile_book(c):
 w=validate(c);rows=[];units=[];words=[v for u in c['units'] for v in u['words']]
 legacy=hashlib.sha256(json.dumps(words,ensure_ascii=False,separators=(',',':')).encode()).hexdigest()=='93da5159c2e99c76ff8abc087bf580d3701511456ba6e7c3f84bc7c9a2640870'
 for n,u in enumerate(c['units']):
  start=len(rows)
  for en,zh in u['words']:rows.append(dict(id=len(rows)+1,text=en,zh=zh,kind='word',unit=n))
  units.append(dict(title=u['title_zh'],word_start=start,word_count=len(u['words'])))
 audio={};audio_rows=[]
 for r in rows:
  spoken=c.get('speech',{}).get(r['text'],r['text'])
  if spoken not in audio:
   audio[spoken]=len(audio_rows)+1;audio_rows.append(dict(id=audio[spoken],text=spoken))
  r['audio']=audio[spoken]
 # The application embeds exactly one voice pack and addresses it by word index,
 # so every word must own its own clip rather than share a de-duplicated one.
 assert len(audio_rows)==w,'Each word needs its own audio id; %u clips for %u words'%(len(audio_rows),w)
 q=lambda x:json.dumps(x,ensure_ascii=False)
 version=zlib.crc32(json.dumps(rows,ensure_ascii=False,sort_keys=True).encode())
 word_version=zlib.crc32(json.dumps(words,ensure_ascii=False).encode())
 migration=c.get('word_migration',{})
 if migration:
  assert len(migration['indices'])==w and all(0<=i<migration['word_count'] for i in migration['indices'])
 header=f'''#pragma once
#include <stdint.h>
#define WORD_COUNT {w}
#define AUDIO_COUNT {len(audio_rows)}
#define UNIT_COUNT {len(units)}
#define CONTENT_VERSION {version}u
#define WORD_CONTENT_VERSION {word_version}u
#define LEGACY_V1_COMPATIBLE {int(legacy)}
#define CONTENT_COMPLETE {int(bool(c.get('complete')))}
#define PREVIOUS_WORD_COUNT {migration.get('word_count',0)}
#define PREVIOUS_WORD_VERSION {migration.get('version',0)}u
typedef struct {{const char *en,*zh;uint16_t audio;}} item_t;
typedef struct {{const char *title;uint16_t word_start,word_count;}} unit_t;
extern const item_t items[WORD_COUNT];
/* `units` groups the word list for the compiler, the illustration sheet and the
   content tests; nothing on screen shows a unit any more. */
extern const char *word_ipa[WORD_COUNT];
extern const unit_t units[UNIT_COUNT];
extern const uint16_t previous_word_indices[WORD_COUNT];
'''
 code='#include "content.h"\nconst item_t items[]={\n'+''.join('{'+q(r['text'])+','+q(r['zh'])+','+str(r['audio'])+'},\n' for r in rows)+'};\nconst unit_t units[]={\n'
 code+=''.join('{'+q(u['title'])+','+','.join(str(u[k]) for k in ('word_start','word_count'))+'},\n' for u in units)+'};\n'
 code+='const uint16_t previous_word_indices[WORD_COUNT]={'+','.join(map(str,migration.get('indices',[0]*w)))+'};\n'
 ipa_list=[c['ipa'][en] for en,zh in words]
 code+='const char *word_ipa[WORD_COUNT]={'+','.join(q(v) for v in ipa_list)+'};\n'
 return header,code,rows,ipa_list
def words_markdown(c,ipa_list):
 # One row per word in sheet order, so a redrawn illustration sheet can be checked
 # against it cell by cell. The picture column is the generated preview's name.
 out=['# 单词表 · %d 个常见英语单词'%len(ipa_list),'',
      '由 `tools/compile_content.py` 从 `content/curriculum.json` 生成，不要手工改这一份。',
      '音标是通用美式（与本版语音一致）。这一份也是插画表的行优先清单：第 N 行对应 `assets/art/words-sheet.webp` 的第 N 格；',
      '配图列 `assets/art/words/` 下那张 PNG 是 `tools/prepare_word_art.py` 切出来的预览，不是输入。','']
 n=0
 for i,u in enumerate(c['units']):
  out+=['## %d %s'%(i+1,u['title_zh']),'','| # | 单词 | 音标 | 中文 | 配图 |','|---|---|---|---|---|']
  for en,zh in u['words']:
   out.append('| %02d | %s | /%s/ | %s | `%02d-%s.png` |'%(n,en,ipa_list[n],zh,n,en));n+=1
  out.append('')
 return '\n'.join(out).rstrip()+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--input',type=Path,default=R/'content/curriculum.json');a=p.parse_args();c=json.loads(a.input.read_text());h,code,rows,ipa_list=compile_book(c)
 audio_rows={r['audio']:dict(id=r['audio'],text=c.get('speech',{}).get(r['text'],r['text'])) for r in rows}
 (R/'main/content.h').write_text(h);(R/'main/content.c').write_text(code);(R/'content/audio-texts.json').write_text(json.dumps(list(audio_rows.values()),ensure_ascii=False,indent=2)+'\n')
 (R/'content/words.md').write_text(words_markdown(c,ipa_list))
 print('Compiled',len(rows),'items without truncation; complete:',bool(c.get('complete')))
