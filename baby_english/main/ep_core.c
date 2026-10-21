#include "ep_core.h"
#include "ep_voice.h"
#include "ep_notices.h"
#include <string.h>
static void select_option(app_t*a,ep_input_event_t e,unsigned count){
 if(e==EP_UP)a->focus=(a->focus+count-1)%count;
 if(e==EP_DOWN)a->focus=(a->focus+1)%count;
}
static void play(app_t*a){a->audio_id=items[a->index].audio;a->audio_seq++;a->playing=true;}
static void stop(app_t*a){a->playing=false;a->audio_id=0;a->audio_seq++;}
void app_init(app_t*a,uint32_t now){memset(a,0,sizeof *a);a->volume=80;a->last_input=now;a->random=now^0x91a62b7du;}
static uint32_t random_next(app_t*a){uint32_t x=a->random;x^=x<<13;x^=x>>17;x^=x<<5;a->random=x;return x;}
/* Lay out all 60 words in a fresh random order. The book is one deck: every card
   is in it, and the order the child sees is not the order the list is written. */
static void deal(app_t*a){
 for(unsigned i=0;i<WORD_COUNT;i++)a->queue[i]=i;
 for(unsigned i=WORD_COUNT;i>1;i--){unsigned j=random_next(a)%i;uint16_t t=a->queue[i-1];a->queue[i-1]=a->queue[j];a->queue[j]=t;}
}
/* HOME starts a shuffled round: all 60 words, each once, in an order the child
   cannot predict -- that is what "首页点一下就开始随机一个图片" needs, and it
   replaces the unit screen entirely. A card opens silent, so a child who only
   wants to look can page through the pictures without hearing anything. */
static void start_cards(app_t*a){
 a->queue_count=WORD_COUNT;a->queue_pos=0;deal(a);
 a->index=a->queue[0];a->page=WORD;stop(a);
}
/* "Next" walks the shuffled round to its end and only then reshuffles, so a word
   cannot come round twice before every other one has been seen. */
static void next_word(app_t*a){
 if(a->queue_pos+1>=a->queue_count){
  unsigned last=a->index;deal(a);
  if(a->queue[0]==last){uint16_t t=a->queue[0];a->queue[0]=a->queue[1];a->queue[1]=t;}
  a->queue_pos=0;
 } else a->queue_pos++;
 a->index=a->queue[a->queue_pos];a->page=WORD;stop(a);
}
static void back(app_t*a){
 stop(a);
 if(a->page==LICENSE){a->page=SETTINGS;a->focus=2;}
 else a->page=HOME;
}
void app_action(app_t*a,ep_input_event_t e,uint32_t now){
 if(e==EP_INPUT_NONE)return;
 a->last_input=now;
 if(a->dimmed||e==EP_WAKE){a->dimmed=false;return;}
 if(e==EP_BACK){back(a);return;}
 switch(a->page){
 case HOME:
  /* Any of the three keys starts a round; holding one down opens settings. */
  if(e==EP_LONG){a->page=SETTINGS;a->focus=0;stop(a);}
  else start_cards(a);
  break;
 case WORD:
  /* The card's three buttons are the three keys, one job each: A (top) leaves the
     card, B (middle) deals the next picture, C (bottom) says the word. There is no
     highlight to move and nothing to confirm, so a child presses the key whose
     button they want -- a hold on a card is deliberately inert. */
  if(e==EP_UP)back(a);
  else if(e==EP_DOWN)next_word(a);
  else if(e==EP_OK)play(a);
  break;
 case SETTINGS:
  select_option(a,e,4);
  if(e==EP_OK){
   if(a->focus==3){back(a);break;}
   if(a->focus==2){a->page=LICENSE;a->focus=0;a->notice_page=0;stop(a);break;}
   unsigned old=a->volume;
   if(a->focus==0)a->volume=a->volume>=10?a->volume-10:0;
   else a->volume=a->volume<=90?a->volume+10:100;
   if(old!=a->volume){a->progress_seq++;a->audio_id=items[0].audio;a->audio_seq++;a->playing=true;}
  }break;
 case LICENSE:
  select_option(a,e,3);
  if(e==EP_OK){
   if(a->focus==2){back(a);break;}
   a->notice_page=(a->notice_page+ep_notice_count+(a->focus?1:-1))%ep_notice_count;
  }break;
 }
}
void app_audio_finished(app_t*a,uint32_t sequence,bool success,uint32_t now){
 (void)success;
 if(a->audio_seq!=sequence)return;
 a->last_input=now;a->playing=false;
}
void app_tick(app_t*a,uint32_t now){
 if(a->playing)a->last_input=now;
 if(!a->dimmed&&(uint32_t)(now-a->last_input)>=60000){a->dimmed=true;stop(a);}
}
static void put32(uint8_t*b,uint32_t v){for(int i=0;i<4;i++)b[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t*b){uint32_t v=0;for(int i=0;i<4;i++)v|=(uint32_t)b[i]<<(8*i);return v;}
size_t app_save(const app_t*a,uint8_t*out,size_t cap){size_t n=APP_SAVE_SIZE;if(cap<n)return 0;memset(out,0,n);memcpy(out,"BE04",4);put32(out+4,WORD_CONTENT_VERSION);out[8]=WORD_COUNT&255;out[9]=WORD_COUNT>>8;out[10]=a->volume;memcpy(out+12,a->mastery,WORD_COUNT);put32(out+n-4,ep_voice_crc32(out,n-4));return n;}
bool app_load(app_t*a,const uint8_t*in,size_t n){
 if(n<16||get32(in+n-4)!=ep_voice_crc32(in,n-4))return false;
 /* The previous automatic familiarity score is not notebook membership. */
 if(n==935&&!memcmp(in,"BE02",4)&&get32(in+4)==2839013132u&&in[8]==151&&in[9]==3&&in[10]<=100&&!in[11]){
  for(unsigned i=0;i<919;i++)if(in[12+i]>3)return false;
  memset(a->mastery,0,sizeof a->mastery);a->volume=in[10];a->progress_seq++;return true;
 }
#if PREVIOUS_WORD_COUNT > 0
 /* Map surviving words by their stable source identity, not their new position. */
 if(n==16u+PREVIOUS_WORD_COUNT&&!memcmp(in,"BE04",4)&&get32(in+4)==PREVIOUS_WORD_VERSION&&
    ((unsigned)in[8]+((unsigned)in[9]<<8))==PREVIOUS_WORD_COUNT&&in[10]<=100&&!in[11]){
  for(unsigned i=0;i<PREVIOUS_WORD_COUNT;i++)if(in[12+i]>2)return false;
  for(unsigned i=0;i<WORD_COUNT;i++)a->mastery[i]=in[12+previous_word_indices[i]];
  a->volume=in[10];a->progress_seq++;return true;
 }
#endif
 if(n!=APP_SAVE_SIZE||memcmp(in,"BE04",4)||get32(in+4)!=WORD_CONTENT_VERSION||((unsigned)in[8]+((unsigned)in[9]<<8))!=WORD_COUNT||in[10]>100||in[11])return false;
 for(unsigned i=0;i<WORD_COUNT;i++)if(in[12+i]>2)return false;
 a->volume=in[10];memcpy(a->mastery,in+12,WORD_COUNT);return true;
}
