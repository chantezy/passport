#include "ep_input.h"
#include <string.h>
/* A press shorter than this reports UP/DOWN/OK on release. Holding it this long
   reports EP_LONG *in place* and swallows the release, which is the only way one
   key can mean "start" on a tap and "settings" on a hold. */
#define LONG_PRESS_MS 1000u
#define DEBOUNCE_MS 30u
void ep_input_init(ep_input_t *i, uint32_t now) {
  memset(i,0,sizeof *i);
  i->stable=i->candidate=-1; i->changed=now;
}
static int raw_key(int mv) {
  if (mv<0) return -2;
  if (mv<150) return 0;
  if (mv<447) return 1;
  if (mv<1900) return 2;
  return -1;
}
static ep_input_event_t tap(int key) { return key==0?EP_UP:key==1?EP_DOWN:EP_OK; }
ep_input_event_t ep_input_tick(ep_input_t *i,int mv,uint32_t now,bool dimmed) {
  int key=raw_key(mv);
  if (key==-2) return EP_INPUT_NONE;
  if (key!=i->candidate) { i->candidate=key; i->changed=now; return EP_INPUT_NONE; }
  if (!i->armed) {
    if (key==-1 && (uint32_t)(now-i->changed)>=DEBOUNCE_MS) i->armed=true;
    return EP_INPUT_NONE;
  }
  if (key!=i->stable && (uint32_t)(now-i->changed)>=DEBOUNCE_MS) {
    int old=i->stable; i->stable=key;
    if (key==-1) {
      /* The gesture is judged at release, so a long hold can still become EP_LONG
         and a wake press stays swallowed down to its release. */
      if (old>=0 && !i->consumed && !i->fired) return tap(old);
      return EP_INPUT_NONE;
    }
    i->pressed=now; i->consumed=dimmed; i->fired=false;
    if (dimmed) return EP_WAKE;
    return EP_INPUT_NONE;
  }
  if (key==i->stable && key>=0 && !i->consumed && !i->fired
      && (uint32_t)(now-i->pressed)>=LONG_PRESS_MS) {
    i->fired=true;
    return EP_LONG;
  }
  return EP_INPUT_NONE;
}
