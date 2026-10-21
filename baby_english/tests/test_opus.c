#include "ep_opus.h"
#include "ep_voice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put16(uint8_t*p,unsigned n){p[0]=n;p[1]=n>>8;}
static void put32(uint8_t*p,uint32_t n){for(int i=0;i<4;i++)p[i]=n>>(8*i);}
static void repair(uint8_t*p){put32(p+56,ep_voice_crc32(p+60,3));put32(p+28,ep_voice_crc32(p+40,20));put32(p+32,ep_voice_crc32(p+60,3));put32(p+36,ep_voice_crc32(p,36));}
int main(int argc,char**argv){
 if(argc==3){FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);assert(p&&fread(p,1,n,f)==(size_t)n);fclose(f);ep_opus_pack_t pack;assert(ep_opus_open(&pack,p,n,(uint32_t)strtoul(argv[2],NULL,10)));printf("Validated %u clips, first ID %u\n",pack.count,pack.first);free(p);return 0;}
 /* One 1-byte packet must carry the clip: 200 samples + 104 skip fits in the
  * single 320-sample frame, and leaves the next frame unconsumed. */
 uint8_t p[63]={0},bad[63];memcpy(p,"EPOP",4);put16(p+4,1);put16(p+6,1);put16(p+8,1);put16(p+10,EP_OPUS_FRAME);put32(p+12,16000);put32(p+16,123);put32(p+20,20);put32(p+24,3);put32(p+44,3);put32(p+48,200);put16(p+52,104);put16(p+60,1);p[62]=0xf8;repair(p);
 ep_opus_pack_t pack;ep_opus_clip_t clip;assert(ep_opus_open(&pack,p,63,123));assert(!ep_opus_open(&pack,p,63,122));assert(ep_opus_open(&pack,p,63,123));assert(ep_opus_clip(&pack,1,&clip));assert(!ep_opus_clip(&pack,0,&clip));assert(!ep_opus_clip(&pack,2,&clip));
 uint32_t pos=0;const uint8_t*b;uint16_t n;assert(ep_opus_packet(&clip,&pos,&b,&n)&&n==1&&pos==3);assert(!ep_opus_packet(&clip,&pos,&b,&n));
 for(size_t i=0;i<63;i++){assert(!ep_opus_open(&pack,p,i,123));memcpy(bad,p,63);bad[i]^=1;assert(!ep_opus_open(&pack,bad,63,123));}
 memcpy(bad,p,63);put32(bad+40,0xfffffff0);repair(bad);assert(!ep_opus_open(&pack,bad,63,123));
 memcpy(bad,p,63);put16(bad+60,99);repair(bad);assert(!ep_opus_open(&pack,bad,63,123));
 memcpy(bad,p,63);put32(bad+48,1000);repair(bad);assert(!ep_opus_open(&pack,bad,63,123));
 memcpy(bad,p,63);put16(bad+52,960);repair(bad);assert(!ep_opus_open(&pack,bad,63,123)); /* a legacy 60 ms skip must not load */
 puts("Opus framing, truncated ranges, CRC, sample and content identity checks passed");return 0;
}
