#include "ep_core.h"
#include "ep_voice.h"
#include <assert.h>
#include <string.h>
static void put32(uint8_t *out, uint32_t value){for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(value>>(8*i));}
static void repair(uint8_t *out, size_t n){put32(out+n-4,ep_voice_crc32(out,n-4));}
/* The preschool word list replaced a 720-word textbook list with no honest
 * correspondence to it, so the positional word migration was removed rather
 * than faked. This file keeps its old name but now pins the opposite contract:
 * an old save must be refused outright, and the learner restarts at zero
 * instead of inheriting scores for words that no longer exist.
 *
 * The notebook is gone, so nothing writes `mastery[]` any more -- but the field
 * and the bytes it occupies stay in the blob, which is what keeps this device's
 * saved volume readable across the upgrade instead of resetting to 80%. */
int main(void){
 /* Literal v0.5.1 slot: 720 familiarity bytes, 16-byte header. CRC covers
  * everything but its own four bytes, so it is a well-formed save of the old
  * format -- it is refused purely because the word list changed. */
 uint8_t old[736]={0};memcpy(old,"BE04",4);put32(old+4,1875204526u);
 old[8]=208;old[9]=2;old[10]=70;old[12]=2;old[12+21]=2;old[12+22]=1;
 put32(old+732,ep_voice_crc32(old,732));
 app_t a;app_init(&a,0);assert(!app_load(&a,old,sizeof old));
 for(unsigned i=0;i<WORD_COUNT;i++)assert(a.mastery[i]==0);
 assert(a.progress_seq==0&&a.volume==80);

 /* The ancient 151-word v0.5.0 format is still recognised, but only its volume
  * survives: an automatic familiarity score is not notebook membership, so
  * every word starts fresh. */
 uint8_t be02[935]={0};memcpy(be02,"BE02",4);put32(be02+4,2839013132u);
 be02[8]=151;be02[9]=3;be02[10]=65;
 put32(be02+931,ep_voice_crc32(be02,931));
 app_t c;app_init(&c,0);assert(app_load(&c,be02,sizeof be02));
 assert(c.volume==65&&c.progress_seq==1);
 for(unsigned i=0;i<WORD_COUNT;i++)assert(c.mastery[i]==0);

 /* The current format round-trips at the new size. */
 a.volume=70;a.mastery[0]=1;a.mastery[1]=2;
 uint8_t saved[APP_SAVE_SIZE];assert(app_save(&a,saved,sizeof saved)==sizeof saved);
 app_t b;app_init(&b,0);assert(app_load(&b,saved,sizeof saved));
 assert(b.mastery[0]==1&&b.mastery[1]==2&&b.volume==70); /* The bytes still travel. */
 assert(app_save(&b,saved,sizeof saved-1)==0);

 /* Every field of the current format is still checked. */
 uint8_t bad[APP_SAVE_SIZE];size_t n=sizeof bad;
 memcpy(bad,saved,n);bad[12]=3;repair(bad,n);assert(!app_load(&b,bad,n));
 memcpy(bad,saved,n);bad[4]^=1;repair(bad,n);assert(!app_load(&b,bad,n));
 memcpy(bad,saved,n);bad[8]=59;repair(bad,n);assert(!app_load(&b,bad,n));
 memcpy(bad,saved,n);bad[11]=1;repair(bad,n);assert(!app_load(&b,bad,n));
 assert(!app_load(&b,saved,n-1));
 uint8_t longer[APP_SAVE_SIZE+1]={0};memcpy(longer,saved,n);
 assert(!app_load(&b,longer,sizeof longer));
 return 0;
}
