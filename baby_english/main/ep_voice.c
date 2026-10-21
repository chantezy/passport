#include "ep_voice.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
uint32_t ep_voice_crc32(const void *bytes, size_t size) {
    static const uint32_t table[16]={
        0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,
        0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const uint8_t *p=bytes; uint32_t crc=0xffffffffu;
    while(size--) {
        crc^=*p++; crc=(crc>>4)^table[crc&15u]; crc=(crc>>4)^table[crc&15u];
    }
    return crc^0xffffffffu;
}
static bool valid_clip(const ep_voice_clip_t *clip) {
    if(!clip || !clip->data || !clip->size || clip->size%EP_VOICE_BLOCK_BYTES || !clip->samples) return false;
    const uint64_t blocks=clip->size/EP_VOICE_BLOCK_BYTES;
    if(clip->samples>blocks*EP_VOICE_BLOCK_SAMPLES ||
       clip->samples<=(blocks-1)*EP_VOICE_BLOCK_SAMPLES) return false;
    return true;
}
bool ep_voice_open(ep_voice_pack_t *out, const void *bytes, size_t size) {
    if(!out) return false;
    memset(out,0,sizeof *out);
    if(!bytes || size<32) return false;
    const uint8_t *p=bytes; uint16_t count=le16(p+6);
    if(memcmp(p,"EPAV",4) || le16(p+4)!=1 || !count || count>4096 ||
       le32(p+8)!=EP_VOICE_SAMPLE_RATE || le16(p+12)!=EP_VOICE_BLOCK_BYTES || le16(p+14)) return false;
    const uint32_t table_size=le32(p+16), payload_size=le32(p+20);
    if(table_size!=(uint32_t)count*20u || table_size>size-32 ||
       payload_size!=size-32-table_size) return false;
    const uint8_t *table=p+32, *payload=table+table_size;
    if(ep_voice_crc32(table,table_size)!=le32(p+24) || ep_voice_crc32(payload,payload_size)!=le32(p+28)) return false;
    uint32_t expected_offset=0;
    for(uint16_t i=0;i<count;++i) {
        const uint8_t *entry=table+(size_t)i*20;
        uint32_t offset=le32(entry+4), length=le32(entry+8);
        if(le16(entry)!=i+1u || le16(entry+2) || offset!=expected_offset ||
           offset>payload_size || length>payload_size-offset) return false;
        ep_voice_clip_t clip={payload+offset,length,le32(entry+12)};
        if(!valid_clip(&clip) || ep_voice_crc32(clip.data,clip.size)!=le32(entry+16)) return false;
        for(uint32_t b=0;b<length;b+=EP_VOICE_BLOCK_BYTES)
            if(clip.data[b+2]>88 || clip.data[b+3]) return false;
        expected_offset=offset+length;
    }
    if(expected_offset!=payload_size) return false;
    *out=(ep_voice_pack_t){p,table,payload,size,payload_size,count};
    return true;
}
bool ep_voice_clip(const ep_voice_pack_t *pack, uint16_t id, ep_voice_clip_t *out) {
    if(!out) return false;
    memset(out,0,sizeof *out);
    if(!pack || !pack->table || !pack->payload || !id || id>pack->count) return false;
    const uint8_t *p=pack->table+(size_t)(id-1)*20;
    uint32_t offset=le32(p+4), size=le32(p+8);
    if(le16(p)!=id || offset>pack->payload_size || size>pack->payload_size-offset) return false;
    *out=(ep_voice_clip_t){pack->payload+offset,size,le32(p+12)};
    return valid_clip(out);
}
bool ep_voice_decoder_init(ep_voice_decoder_t *out, const ep_voice_clip_t *clip) {
    if(!out) return false;
    memset(out,0,sizeof *out);
    if(!valid_clip(clip)) { out->failed=true; return false; }
    out->clip=*clip; return true;
}
size_t ep_voice_decode(ep_voice_decoder_t *d, int16_t *pcm, size_t capacity) {
    static const int steps[89]={
        7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
        66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,
        371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
        1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,
        5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,
        16818,18500,20350,22385,24623,27086,29794,32767};
    static const int8_t shifts[8]={-1,-1,-1,-1,2,4,6,8};
    if(!d || d->failed || !capacity) return 0;
    if(!pcm || !valid_clip(&d->clip)) { d->failed=true; return 0; }
    size_t written=0;
    while(written<capacity && d->position<d->clip.samples) {
        uint32_t within=d->position%EP_VOICE_BLOCK_SAMPLES;
        uint32_t block=(d->position/EP_VOICE_BLOCK_SAMPLES)*EP_VOICE_BLOCK_BYTES;
        const uint8_t *p=d->clip.data+block;
        if(!within) {
            if(p[2]>88 || p[3]) { d->failed=true; break; }
            uint16_t raw=le16(p);
            d->predictor=raw<0x8000u?(int32_t)raw:(int32_t)raw-65536;
            d->step_index=p[2];
        } else {
            uint32_t nibble=within-1;
            uint8_t code=(p[4+nibble/2]>>((nibble&1u)*4))&15u;
            int step=steps[d->step_index], delta=step>>3;
            if(code&1) delta+=step>>2;
            if(code&2) delta+=step>>1;
            if(code&4) delta+=step;
            d->predictor+=(code&8)?-delta:delta;
            if(d->predictor>32767) d->predictor=32767;
            if(d->predictor<-32768) d->predictor=-32768;
            d->step_index+=shifts[code&7];
            if(d->step_index<0) d->step_index=0;
            if(d->step_index>88) d->step_index=88;
        }
        pcm[written++]=(int16_t)d->predictor; ++d->position;
    }
    return written;
}
