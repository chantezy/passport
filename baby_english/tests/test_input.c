#include "ep_input.h"
#include <assert.h>
#include <stdio.h>

static void release_boot(ep_input_t *i) {
  assert(ep_input_tick(i,2500,0,false)==EP_INPUT_NONE);
  assert(ep_input_tick(i,2500,30,false)==EP_INPUT_NONE);
}
int main(void) {
  ep_input_t i; ep_input_init(&i,0); release_boot(&i);
  /* A key changing its mind inside the debounce window never becomes a press. */
  assert(ep_input_tick(&i,300,40,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,69,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,80,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,109,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,110,false)==EP_INPUT_NONE); /* A blip is a blip, not a tap. */
  /* A tap reports when the key comes back up, not when it goes down. */
  assert(ep_input_tick(&i,300,200,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,229,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,230,false)==EP_INPUT_NONE); /* Stable DOWN, still silent. */
  assert(ep_input_tick(&i,300,700,false)==EP_INPUT_NONE); /* Held 470 ms: no auto-repeat. */
  assert(ep_input_tick(&i,300,1100,false)==EP_INPUT_NONE);/* Held 870 ms: still nothing. */
  assert(ep_input_tick(&i,2500,1110,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1139,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1140,false)==EP_DOWN); /* The tap lands on release. */
  /* The other two keys report the same way. */
  assert(ep_input_tick(&i,1000,1200,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,1229,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,1230,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1240,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1269,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1270,false)==EP_OK);
  assert(ep_input_tick(&i,100,1300,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,100,1329,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,100,1330,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1340,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1369,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1370,false)==EP_UP);
  assert(ep_input_tick(&i,-1,1400,false)==EP_INPUT_NONE); /* An invalid sample is not a key. */

  /* Holding a key fires EP_LONG once, swallows the release, and never repeats. */
  assert(ep_input_tick(&i,300,2000,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,2029,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,2030,false)==EP_INPUT_NONE); /* Stable, long clock starts here. */
  assert(ep_input_tick(&i,300,3029,false)==EP_INPUT_NONE); /* 999 ms is still a tap. */
  assert(ep_input_tick(&i,300,3030,false)==EP_LONG);
  assert(ep_input_tick(&i,300,3040,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,300,4000,false)==EP_INPUT_NONE); /* Held four seconds: one event only. */
  assert(ep_input_tick(&i,2500,4100,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,4130,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,4200,false)==EP_INPUT_NONE); /* The release adds no tap. */
  /* Every key holds the same way -- 上 and 下 do not get a different gesture. */
  assert(ep_input_tick(&i,100,5000,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,100,5029,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,100,5030,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,100,6030,false)==EP_LONG);
  assert(ep_input_tick(&i,2500,6100,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,6130,false)==EP_INPUT_NONE);

  /* A press that wakes the book is spent on the wake: no tap, no long press. */
  assert(ep_input_tick(&i,1000,7000,true)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,7030,true)==EP_WAKE);
  assert(ep_input_tick(&i,1000,8030,true)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,9000,true)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,9100,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,9130,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,9200,false)==EP_INPUT_NONE);
  /* The gesture after the wake behaves normally again. */
  assert(ep_input_tick(&i,1000,9300,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,9329,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,9330,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,9400,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,9430,false)==EP_OK);

  ep_input_init(&i,0);
  assert(ep_input_tick(&i,1000,0,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,1000,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1100,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1130,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,1200,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,1000,1230,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1300,false)==EP_INPUT_NONE);
  assert(ep_input_tick(&i,2500,1330,false)==EP_OK); /* Held at boot must first release. */
  puts("input debounce, release-reported taps, one-shot long press, wake swallowing PASS");
  return 0;
}
