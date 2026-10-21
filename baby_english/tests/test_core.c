#include "ep_core.h"
#include "ep_notices.h"
#include "ep_voice.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Deal one card at a time until the lap ends, recording the order dealt. */
static void lap(app_t*a,uint16_t*order,uint32_t base){
 for(unsigned i=0;i<WORD_COUNT;i++){
  assert(a->page==WORD&&a->queue_count==WORD_COUNT&&a->queue_pos==i);
  assert(a->index==a->queue[i]);
  order[i]=a->index;
  app_action(a,EP_DOWN,base+i);
 }
}
int main(void){
 app_t a;app_init(&a,0);assert(a.volume==80&&a.page==HOME);
 /* The whole book now saves in the same 76-byte BE04 blob it always has. */
 assert(APP_SAVE_SIZE==76u);

 /* HOME is a picture: any of the three keys deals a card, holding one opens settings. */
 for(int k=0;k<3;k++){
  app_t h;app_init(&h,0);app_action(&h,k==0?EP_UP:k==1?EP_DOWN:EP_OK,1);
  assert(h.page==WORD&&h.queue_count==WORD_COUNT&&h.queue_pos==0);
  assert(!h.playing&&h.audio_id==0); /* A card always opens silent. */
 }
 app_t g;app_init(&g,0);app_action(&g,EP_LONG,1);
 assert(g.page==SETTINGS&&g.focus==0&&!g.playing);
 app_action(&g,EP_LONG,2);assert(g.page==SETTINGS); /* A hold in settings navigates nothing. */

 /* A card gives each key one job: A leaves it, B deals the next picture, C says
    the word. Nothing is highlighted, so no key has to move a focus first. */
 app_t b;app_init(&b,0);app_action(&b,EP_OK,1);
 assert(b.page==WORD&&b.index==b.queue[0]);
 uint16_t shown=b.index;
 app_action(&b,EP_OK,2);
 assert(b.playing&&b.audio_id==items[shown].audio&&b.page==WORD&&b.index==shown);
 uint32_t seq=b.audio_seq;
 app_audio_finished(&b,seq,true,3);assert(!b.playing&&b.index==shown); /* Silence leaves the card put. */
 app_action(&b,EP_LONG,4);assert(b.page==WORD&&b.index==shown&&!b.playing); /* A hold on a card is inert. */
 app_action(&b,EP_OK,5);assert(b.playing);
 app_action(&b,EP_DOWN,6);
 assert(b.index==b.queue[1]&&b.index!=shown&&!b.playing&&b.audio_id==0&&b.page==WORD);
 /* A is the way off a card, and leaving it stops the voice. */
 app_action(&b,EP_UP,7);assert(b.page==HOME&&!b.playing);

 /* One lap is a permutation: every word once, and not in list order. */
 app_t r;app_init(&r,0);app_action(&r,EP_OK,1);
 uint16_t first_lap[WORD_COUNT];bool hit[WORD_COUNT];memset(hit,0,sizeof hit);bool shuffled=false;
 lap(&r,first_lap,100);
 for(unsigned i=0;i<WORD_COUNT;i++){assert(first_lap[i]<WORD_COUNT&&!hit[first_lap[i]]);hit[first_lap[i]]=true;if(i&&first_lap[i]<first_lap[i-1])shuffled=true;}
 for(unsigned i=0;i<WORD_COUNT;i++)assert(hit[i]);
 assert(shuffled);
 /* The lap then wraps and reshuffles, and never opens on the card just left. */
 assert(r.page==WORD&&r.queue_pos==0&&r.index==r.queue[0]);
 uint16_t second_lap[WORD_COUNT];
 lap(&r,second_lap,200);
 assert(second_lap[0]!=first_lap[WORD_COUNT-1]);
 assert(memcmp(first_lap,second_lap,sizeof first_lap)!=0);
 /* The deal depends on when the book was switched on, so it is not a fixed order. */
 app_t x,y;app_init(&x,1);app_init(&y,2);app_action(&x,EP_OK,3);app_action(&y,EP_OK,3);
 bool differ=false;for(unsigned i=0;i<WORD_COUNT;i++)if(x.queue[i]!=y.queue[i])differ=true;assert(differ);

 /* SETTINGS owns the volume and the only door into LICENSE. */
 app_t s;app_init(&s,0);app_action(&s,EP_LONG,1);assert(s.page==SETTINGS&&s.focus==0);
 app_action(&s,EP_UP,2);assert(s.focus==3);app_action(&s,EP_DOWN,3);assert(s.focus==0);
 app_action(&s,EP_OK,4);assert(s.volume==70&&s.progress_seq==1&&s.playing); /* The change previews. */
 app_action(&s,EP_OK,5);assert(s.volume==60&&s.progress_seq==2);
 app_action(&s,EP_DOWN,6);app_action(&s,EP_DOWN,7);assert(s.focus==2);
 app_action(&s,EP_OK,8);assert(s.page==LICENSE&&s.focus==0&&s.notice_page==0&&!s.playing);
 app_action(&s,EP_DOWN,9);assert(s.focus==1&&s.notice_page==0);
 app_action(&s,EP_OK,10);assert(s.notice_page==1);
 s.focus=0;app_action(&s,EP_OK,11);assert(s.notice_page==0);
 s.focus=0;app_action(&s,EP_OK,12);assert(s.notice_page==ep_notice_count-1); /* Wraps backwards. */
 s.focus=1;app_action(&s,EP_OK,13);assert(s.notice_page==0);
 /* 返回 climbs one level at a time, and settings' last entry is the way home. */
 s.focus=2;app_action(&s,EP_OK,14);assert(s.page==SETTINGS&&s.focus==2&&s.notice_page==0);
 s.focus=3;app_action(&s,EP_OK,15);assert(s.page==HOME);
 s.page=LICENSE;s.focus=1;app_action(&s,EP_BACK,16);assert(s.page==SETTINGS&&s.focus==2);
 app_action(&s,EP_BACK,17);assert(s.page==HOME); /* The hardware has no back key, tests do. */
 s.page=WORD;app_action(&s,EP_UP,18);assert(s.page==HOME);

 /* 60 s of quiet dims the book; sound and the next press both keep it awake. */
 app_t d;app_init(&d,0);app_action(&d,EP_OK,1);assert(d.page==WORD);
 app_tick(&d,60000);assert(!d.dimmed);
 app_tick(&d,60001);assert(d.dimmed&&!d.playing);
 app_action(&d,EP_OK,60002);assert(!d.dimmed&&d.page==WORD); /* A press only wakes it. */
 app_action(&d,EP_OK,60003);assert(d.playing);
 app_tick(&d,120004);assert(!d.dimmed); /* Playing counts as activity. */
 uint32_t dseq=d.audio_seq;app_audio_finished(&d,dseq-1,true,120005);assert(d.playing);
 app_audio_finished(&d,dseq,true,120006);assert(!d.playing);

 /* The volume survives a reboot in the same 76-byte blob, and junk is refused. */
 uint8_t buf[APP_SAVE_SIZE];size_t n=app_save(&s,buf,sizeof buf);assert(n==APP_SAVE_SIZE);
 app_t c;app_init(&c,0);assert(app_load(&c,buf,n)&&c.volume==s.volume&&c.page==HOME);
 for(size_t i=0;i<n;i++){buf[i]^=1;assert(!app_load(&c,buf,n));buf[i]^=1;}
 assert(!app_load(&c,buf,n-1)&&!app_save(&s,buf,1));
 uint8_t legacy[935]={0};memcpy(legacy,"BE02",4);uint32_t cv=2839013132u;
 for(int i=0;i<4;i++)legacy[4+i]=(uint8_t)(cv>>(8*i));
 legacy[8]=151;legacy[9]=3;legacy[10]=90;memset(legacy+12,3,919);
 uint32_t crc=ep_voice_crc32(legacy,931);for(int i=0;i<4;i++)legacy[931+i]=(uint8_t)(crc>>(8*i));
 assert(app_load(&c,legacy,935)&&c.volume==90);
 for(unsigned i=0;i<WORD_COUNT;i++)assert(c.mastery[i]==0); /* The old scores are not carried over. */
 legacy[50]^=1;assert(!app_load(&c,legacy,935));

 puts("Any key deals a card, hold opens settings, one shuffled no-repeat lap, one key per card button, back-to-home, dimming and persistence passed");
}
