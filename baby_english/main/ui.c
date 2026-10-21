#include "ui.h"
#include "ep_notices.h"
#include "home_art.h"
#include "ui_art.h"
#include "word_art.h"
#include "src/misc/lv_text_private.h"
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(ep_font12);LV_FONT_DECLARE(ep_font14);LV_FONT_DECLARE(ep_font16);LV_FONT_DECLARE(ep_font20);LV_FONT_DECLARE(ep_font24);

static lv_obj_t *screen;static app_t prior;static unsigned prior_errors=99;
static lv_obj_t* label(const char*s,int x,int y,int w,const lv_font_t*f,uint32_t color){lv_obj_t*o=lv_label_create(screen);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,f,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_line_space(o,3,0);return o;}
static void text(const char*s,int y,const lv_font_t*f){lv_obj_t*o=label(s,18,y,204,f,0x194E40);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);}
static void fitted(const char*s,int y,int height,bool chinese){
 const lv_font_t*fonts[]={&ep_font24,&ep_font20,&ep_font16,&ep_font14,&ep_font12};
 for(unsigned i=chinese?3:0;i<5;i++){lv_point_t z;lv_text_get_size(&z,s,fonts[i],0,3,204,0);if(z.y<=height){text(s,y,fonts[i]);return;}}
 text(s,y,&ep_font12); /* Every complete card is verified by the native layout test. */
}
/* Every page but HOME carries one picture: no mascot, no motion. */
static void icon(const lv_image_dsc_t*src,int x,int y){lv_obj_t*o=lv_image_create(screen);lv_image_set_src(o,src);lv_obj_set_pos(o,x,y);}
static void option(const char*s,int y,bool selected){lv_obj_t*o=lv_obj_create(screen);lv_obj_set_pos(o,16,y);lv_obj_set_size(o,208,31);lv_obj_set_style_radius(o,10,0);lv_obj_set_style_bg_color(o,lv_color_hex(selected?0x205C48:0xECE6D5),0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_scrollable(o,false);lv_obj_t*l=label(s,24,y+5,192,&ep_font16,selected?0xFFFFFF:0x194E40);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);}
static void small_option(const char*s,int x,int y,int width,bool selected){
 lv_obj_t*o=lv_obj_create(screen);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,29);lv_obj_set_style_radius(o,8,0);lv_obj_set_style_bg_color(o,lv_color_hex(selected?0x205C48:0xECE6D5),0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_scrollable(o,false);
 lv_obj_t*l=label(s,x+3,y+6,width-6,&ep_font12,selected?0xFFFFFF:0x194E40);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
}
lv_obj_t*ui_screen(void){return screen;}
void ui_render(const app_t*a,bool ae,bool se,bool ie){unsigned errors=ae|se<<1|ie<<2;app_t state=*a;state.last_input=0;state.audio_seq=0;state.progress_seq=0;if(screen&&!memcmp(&state,&prior,sizeof state)&&errors==prior_errors)return;prior=state;prior_errors=errors;
 if(!screen){screen=lv_obj_create(NULL);lv_screen_load(screen);}lv_obj_clean(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(0xF7F3E7),0);lv_obj_set_scrollable(screen,false);
 char s[128];
 /* The backdrop already carries the product name, so HOME is the picture plus one
    line saying how to get off it: tap any key for a card, hold one for settings. */
 if(a->page==HOME){icon(home_image(),0,0);text("单击开始 · 长按设置",292,&ep_font12);}
 else if(a->page==WORD){
  /* The 112x112 picture and the Chinese gloss are both always on screen; the
     three small buttons underneath are the card's only controls, and each one is
     labelled with the key that fires it -- there is no highlight to move, so none
     of them is drawn selected. */
  snprintf(s,sizeof s,"背单词 %u / %u",a->queue_pos+1,a->queue_count);text(s,10,&ep_font14);
  icon(word_image(a->index),64,34);
  fitted(items[a->index].en,150,32,false);
  snprintf(s,sizeof s,"/%s/",word_ipa[a->index]);text(s,186,&ep_font16);
  fitted(items[a->index].zh,212,34,true);
  small_option("A 返回",16,270,66,false);small_option("B 下一个",87,270,66,false);small_option("C 发声",158,270,66,false);
 }else if(a->page==SETTINGS){text("音量与说明",12,&ep_font20);icon(ui_art_image(UI_ART_SPEAKER),88,43);snprintf(s,sizeof s,"音量 %u%%",a->volume);text(s,115,&ep_font24);const char*opts[]={"减小音量并试听","增大音量并试听","查看字体许可","返回首页"};for(unsigned i=0;i<4;i++)option(opts[i],168+i*32,a->focus==i);text("A / B 切换 · C 确认",298,&ep_font12);
 }else {snprintf(s,sizeof s,"字体许可 %u / %u",a->notice_page+1,ep_notice_count);text(s,12,&ep_font16);label(ep_notice_pages[a->notice_page%ep_notice_count],16,43,208,&ep_font12,0x194E40);small_option("上一页",16,263,66,a->focus==0);small_option("下一页",87,263,66,a->focus==1);small_option("返回",158,263,66,a->focus==2);text("A / B 切换 · C 确认",298,&ep_font12);}

 if(errors){lv_obj_t*o=label(ie?"按键异常":ae?"声音暂不可用":"进度保存失败",26,71,188,&ep_font14,0xB62E28);lv_obj_set_style_bg_color(o,lv_color_hex(0xFFF0D0),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);}
}
