#include "ep_opus.h"
#include "ep_voice.h"
#include <string.h>
static uint16_t u16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool ep_opus_packet(const ep_opus_clip_t*c,uint32_t*pos,const uint8_t**data,uint16_t*size){
 if(!c||!pos||!data||!size||*pos>c->bytes||c->bytes-*pos<2)return false;
 uint16_t n=u16(c->data+*pos);if(!n||n>1275||n>c->bytes-*pos-2)return false;
 *data=c->data+*pos+2;*size=n;*pos+=n+2;return true;
}
bool ep_opus_clip(const ep_opus_pack_t*p,uint16_t id,ep_opus_clip_t*out){
 if(!p||!out||!p->table||id<p->first||(uint32_t)id-p->first>=p->count)return false;
 const uint8_t*e=p->table+((uint32_t)id-p->first)*20;
 uint32_t off=u32(e),len=u32(e+4);if(off>p->size||len>p->size-off)return false;
 *out=(ep_opus_clip_t){p->data+off,len,u32(e+8),u16(e+12)};return true;
}
bool ep_opus_open(ep_opus_pack_t*out,const void*bytes,size_t size,uint32_t content){
 if(!out)return false;
 memset(out,0,sizeof *out);if(!bytes||size<40)return false;
 const uint8_t*p=bytes;uint16_t first=u16(p+6),count=u16(p+8);uint32_t table=u32(p+20),data=u32(p+24);
 if(memcmp(p,"EPOP",4)||u16(p+4)!=1||!first||!count||(uint32_t)first+count>4097||u16(p+10)!=EP_OPUS_FRAME||u32(p+12)!=16000||u32(p+16)!=content||table!=(uint32_t)count*20||table>size-40||data>size-40-table||ep_voice_crc32(p,36)!=u32(p+36))return false;
 /* A partition can have erased trailing bytes; header sizes delimit the exact pack. */
 if(ep_voice_crc32(p+40,table)!=u32(p+28)||ep_voice_crc32(p+40+table,data)!=u32(p+32))return false;
 ep_opus_pack_t pack={p+40,p+40+table,data,first,count};uint32_t expected=0;
 for(uint16_t i=0;i<count;i++){
  const uint8_t*e=pack.table+(uint32_t)i*20;ep_opus_clip_t c;
  if(u32(e)!=expected||u16(e+14)||!ep_opus_clip(&pack,first+i,&c)||!c.samples||c.samples>16000*180||c.skip>=EP_OPUS_FRAME||ep_voice_crc32(c.data,c.bytes)!=u32(e+16))return false;
  uint32_t pos=0,frames=0;const uint8_t*packet;uint16_t n;
  while(pos<c.bytes){if(!ep_opus_packet(&c,&pos,&packet,&n))return false;frames++;}
  if((uint64_t)frames*EP_OPUS_FRAME<c.samples+c.skip||(uint64_t)frames*EP_OPUS_FRAME>=c.samples+c.skip+EP_OPUS_FRAME)return false;
  expected+=c.bytes;
 }
 if(expected!=data)return false;
 *out=pack;return true;
}
