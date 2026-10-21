#include "ep_voice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put16(uint8_t *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t *p, uint32_t v) { for(int i=0;i<4;++i) p[i]=v>>(8*i); }
static void repair(uint8_t *p) {
    put32(p+48, ep_voice_crc32(p+52,256));
    put32(p+24, ep_voice_crc32(p+32,20));
    put32(p+28, ep_voice_crc32(p+52,256));
}
static void fixture(uint8_t *p) {
    memset(p,0,308); memcpy(p,"EPAV",4); put16(p+4,1); put16(p+6,1);
    put32(p+8,16000); put16(p+12,256); put32(p+16,20); put32(p+20,256);
    put16(p+32,1); put32(p+40,256); put32(p+44,505);
    put16(p+52,1000); repair(p);
}
static void test_pack(void) {
    uint8_t p[308], bad[309]; ep_voice_pack_t pack; ep_voice_clip_t clip;
    fixture(p); assert(ep_voice_crc32("123456789",9)==0xcbf43926u);
    assert(ep_voice_open(&pack,p,sizeof p)); assert(pack.count==1);
    assert(!ep_voice_clip(&pack,0,&clip)); assert(!ep_voice_clip(&pack,2,&clip));
    assert(ep_voice_clip(&pack,1,&clip)); assert(clip.samples==505);
    for(size_t n=0;n<sizeof p;++n) assert(!ep_voice_open(&pack,p,n));
    memcpy(bad,p,sizeof p); bad[308]=0; assert(!ep_voice_open(&pack,bad,sizeof bad));
    const size_t corrupt[]={0,4,6,8,12,14,16,20,24,28,32,34,36,40,44,48,52,55,57,307};
    for(size_t n=0;n<sizeof corrupt/sizeof corrupt[0];++n) {
        memcpy(bad,p,sizeof p); bad[corrupt[n]]^=1;
        assert(!ep_voice_open(&pack,bad,sizeof p));
    }
    memcpy(bad,p,sizeof p); bad[54]=89; repair(bad);
    assert(!ep_voice_open(&pack,bad,sizeof p));
    memcpy(bad,p,sizeof p); put32(bad+44,506); repair(bad);
    assert(!ep_voice_open(&pack,bad,sizeof p));
    memcpy(bad,p,sizeof p); put32(bad+36,0xfffffff0u); repair(bad);
    assert(!ep_voice_open(&pack,bad,sizeof p));
    assert(!ep_voice_open(NULL,p,sizeof p)); assert(!ep_voice_open(&pack,NULL,sizeof p));
}
static void test_chunks(void) {
    uint8_t p[308]; fixture(p);
    ep_voice_pack_t pack; ep_voice_clip_t clip; ep_voice_decoder_t decoder;
    int16_t pcm[505], chunked[505]; assert(ep_voice_open(&pack,p,sizeof p));
    assert(ep_voice_clip(&pack,1,&clip)); assert(ep_voice_decoder_init(&decoder,&clip));
    assert(ep_voice_decode(&decoder,pcm,505)==505); assert(pcm[0]==1000);
    for(size_t i=1;i<505;++i) assert(pcm[i]==1000);
    assert(ep_voice_decode(&decoder,pcm,1)==0); assert(!decoder.failed);
    assert(ep_voice_decoder_init(&decoder,&clip));
    size_t total=0, n;
    while((n=ep_voice_decode(&decoder,chunked+total,7))) total+=n;
    assert(total==505 && memcmp(pcm,chunked,sizeof pcm)==0);
    assert(!ep_voice_decoder_init(&decoder,NULL));
    assert(ep_voice_decoder_init(&decoder,&clip)); p[54]=99;
    assert(ep_voice_decode(&decoder,pcm,1)==0 && decoder.failed);
}
/* Integration mode: decode any real clip to stdout for independent comparison. */
int main(int argc, char **argv) {
    if(argc==3) {
        FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
        fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
        uint8_t *bytes=malloc((size_t)size); if(!bytes) return 2;
        assert(fread(bytes,1,(size_t)size,f)==(size_t)size); fclose(f);
        ep_voice_pack_t pack; ep_voice_clip_t clip; ep_voice_decoder_t decoder;
        if(!ep_voice_open(&pack,bytes,(size_t)size)) { free(bytes); return 3; }
        if(!ep_voice_clip(&pack,(uint16_t)atoi(argv[2]),&clip)) { free(bytes); return 4; }
        assert(ep_voice_decoder_init(&decoder,&clip)); int16_t pcm[137]; size_t n;
        while((n=ep_voice_decode(&decoder,pcm,137))) fwrite(pcm,2,n,stdout);
        bool failed=decoder.failed; free(bytes); return failed?5:0;
    }
    test_pack(); test_chunks(); puts("voice parser and chunk tests passed"); return 0;
}
