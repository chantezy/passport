#include "ui.h"
#include "ep_notices.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static uint8_t pixels[240*320*3];
static const char *outdir;
static unsigned checked;static size_t peak_used;
static lv_area_t text_areas[80];static const char *text_values[80];static unsigned text_count;
static void no_overlap(lv_obj_t *o){
 if(lv_obj_check_type(o,&lv_label_class)){
  lv_point_t z;const char *v=lv_label_get_text(o);
  lv_text_get_size(&z,v,lv_obj_get_style_text_font(o,0),0,lv_obj_get_style_text_line_space(o,0),lv_obj_get_width(o),0);
  lv_area_t a;lv_obj_get_coords(o,&a);lv_text_align_t align=lv_obj_get_style_text_align(o,0);
  if(align==LV_TEXT_ALIGN_CENTER)a.x1+=(lv_obj_get_width(o)-z.x)/2;
  if(align==LV_TEXT_ALIGN_RIGHT)a.x1+=lv_obj_get_width(o)-z.x;
  a.x2=a.x1+z.x-1;a.y2=a.y1+z.y-1;
  for(unsigned i=0;i<text_count;i++){lv_area_t b=text_areas[i];if(a.x1<=b.x2&&a.x2>=b.x1&&a.y1<=b.y2&&a.y2>=b.y1){fprintf(stderr,"OVERLAP: %s / %s\n",v,text_values[i]);exit(1);}}
  assert(text_count<80);text_areas[text_count]=a;text_values[text_count++]=v;
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)no_overlap(lv_obj_get_child(o,i));
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
  unsigned v=p[0]|p[1]<<8;p+=2;if(x<0||x>=240||y<0||y>=320)continue;
  unsigned n=(y*240+x)*3;pixels[n]=((v>>11)&31)*255/31;pixels[n+1]=((v>>5)&63)*255/63;pixels[n+2]=(v&31)*255/31;
 }lv_display_flush_ready(d);
}
static void bounds(lv_obj_t *o){
 if(lv_obj_check_type(o,&lv_label_class)){
  const char *s=lv_label_get_text(o);const lv_font_t *f=lv_obj_get_style_text_font(o,0);
  lv_area_t a;lv_obj_get_coords(o,&a);lv_point_t sz;
  lv_text_get_size(&sz,s,f,0,lv_obj_get_style_text_line_space(o,0),lv_obj_get_width(o),0);
  if(a.x1<8||a.x2>=232||a.y1<7||a.y2>=313||sz.y>lv_obj_get_height(o)){
   fprintf(stderr,"CLIPPED: %s (%d,%d..%d,%d) text=%d height=%d\n",s,a.x1,a.y1,a.x2,a.y2,sz.y,lv_obj_get_height(o));exit(1);
  }
  for(uint32_t i=0;s[i];){uint32_t cp=lv_text_encoded_next(s,&i);lv_font_glyph_dsc_t g;if(cp!='\n'&&cp!='\r'&&!f->get_glyph_dsc(f,&g,cp,0)){fprintf(stderr,"MISSING GLYPH U+%04x in %s\n",cp,s);exit(1);}}
  checked++;
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)bounds(lv_obj_get_child(o,i));
}
static void shot(app_t *a,const char *name){
 ui_render(a,false,false,false);lv_obj_update_layout(ui_screen());lv_refr_now(NULL);bounds(ui_screen());text_count=0;no_overlap(ui_screen());lv_mem_monitor_t mem;lv_mem_monitor(&mem);size_t used=mem.total_size-mem.free_size;if(used>peak_used)peak_used=used;
 /* Validate every card above; keep only representative raster evidence. */
 if(!strncmp(name,"word-",5)&&a->index%10&&a->index!=WORD_COUNT-1)return;
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",outdir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");fwrite(pixels,1,sizeof pixels,f);fclose(f);
}
int main(int argc,char **argv){assert(argc==2);outdir=argv[1];mkdir(outdir,0755);lv_init();lv_display_t*d=lv_display_create(240,320);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);static uint8_t buf[240*20*2];lv_display_set_buffers(d,buf,NULL,sizeof buf,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 app_t a;app_init(&a,0);assert(a.page==HOME);shot(&a,"01-home");
 /* Any key leaves the picture and deals a card, silently. */
 app_action(&a,EP_OK,1);assert(a.page==WORD&&!a.playing);shot(&a,"02-word-default");
 char name[64];
 /* Every word: the long spellings, the IPA and the Chinese gloss are all rendered
    and measured, not sampled. A card draws no highlight any more -- its three
    buttons are three separate keys -- so one raster per word covers the page. */
 for(unsigned i=0;i<WORD_COUNT;i++){
  a.page=WORD;a.index=i;a.queue_count=WORD_COUNT;a.queue_pos=i;a.playing=false;
  snprintf(name,sizeof name,"word-%02u-card",i);shot(&a,name);
 }
 /* A hold on the picture opens settings; its last entry goes home. */
 app_t h;app_init(&h,0);app_action(&h,EP_LONG,1);assert(h.page==SETTINGS&&h.focus==0);
 for(unsigned f=0;f<4;f++){h.focus=f;snprintf(name,sizeof name,"03-settings-%u",f);shot(&h,name);}
 h.page=LICENSE;
 for(unsigned i=0;i<ep_notice_count;i++){h.notice_page=i;h.focus=2;snprintf(name,sizeof name,"04-license-%02u",i);shot(&h,name);}
 h.page=LICENSE;h.notice_page=0;h.focus=0;shot(&h,"04-license-first");h.focus=1;shot(&h,"04-license-next");
 printf("Checked %u labels, peak LVGL allocation %zu bytes\n",checked,peak_used);return 0;}
