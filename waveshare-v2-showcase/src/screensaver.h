#pragma once
#include <Preferences.h>
#include <math.h>
#include "classic_savers.h"
namespace Saver {
static Preferences prefs;
static uint8_t mode=0; // 0 black, 1 clock, 2 dot, 3 starfield, 4 lines, 5 bubbles, 6 classic stars
static uint32_t timeoutSec=60;
static uint8_t brightness=128;
static uint32_t lastActivity=0, lastFrame=0;
static bool active=false, suppressTouch=false, wakeArmed=false, wakeRequested=false;
static lv_obj_t *screen=nullptr,*label=nullptr,*dots[12]={};
static lv_obj_t *returnScreen=nullptr;
static int16_t x=50,y=80,vx=2,vy=2;
static void begin(){
  prefs.begin("screensaver",false);
  mode=prefs.getUChar("mode",0);
  if(mode>6)mode=0;
  timeoutSec=prefs.getUInt("timeout",60);
  if(timeoutSec>3600)timeoutSec=60;
  lastActivity=millis();
}
static void save(uint8_t m,uint32_t timeout){
  mode=m>6?0:m;
  timeoutSec=timeout>3600?3600:timeout;
  prefs.putUChar("mode",mode);
  prefs.putUInt("timeout",timeoutSec);
  lastActivity=millis();
}
static void activity(){lastActivity=millis();}
static void stop(){
  if(!active)return;
  active=false;
  wakeRequested=false;
  suppressTouch=true;
  if(screen){
    lv_obj_t *old=screen;
    screen=nullptr;label=nullptr;
    if(returnScreen)lv_screen_load(returnScreen);
    returnScreen=nullptr;
    lv_obj_delete(old);
  }
  ledcWrite(5,brightness);
  lastActivity=millis();
}
static void start(){
  if(active)return;
  active=true;
  wakeRequested=false;
  wakeArmed=false; // Preview starts on button release; require a fresh touch.
  if(mode==0){ledcWrite(5,0);return;}
  returnScreen=lv_screen_active();
  screen=lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen,lv_color_black(),0);
  lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
  lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  if(mode==1){
    label=lv_label_create(screen);
    lv_obj_set_style_text_color(label,lv_color_white(),0);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_28,0);
    lv_obj_center(label);
  }else if(mode>=4){
    ClassicSavers::init(screen,mode);
  }else{
    const int count=mode==2?1:12;
    for(int i=0;i<count;++i){
      dots[i]=lv_obj_create(screen);
      lv_obj_set_size(dots[i],mode==2?24:3,mode==2?24:3);
      lv_obj_set_style_radius(dots[i],LV_RADIUS_CIRCLE,0);
      lv_obj_set_style_border_width(dots[i],0,0);
      lv_obj_set_style_pad_all(dots[i],0,0);
      lv_obj_set_style_bg_color(dots[i],lv_color_white(),0);
      lv_obj_set_pos(dots[i],(i*71+37)%240,(i*43+19)%320);
    }
  }
  lv_screen_load(screen);
  lastFrame=0;
}
static void tick(bool touching,bool inhibit){
  const uint32_t now=millis();
  if(!touching) {
    wakeArmed=true;
    if(suppressTouch)suppressTouch=false;
  }
  if(wakeRequested)stop();
  if(touching)activity();
  if(inhibit){activity();return;}
  if(!active){
    if(timeoutSec && !touching && !suppressTouch && (uint32_t)(now-lastActivity)>=timeoutSec*1000UL)start();
    return;
  }
  if(mode==0 || now-lastFrame<80)return;
  lastFrame=now;
  if(mode>=4){
    ClassicSavers::tick(mode);
  }else if(mode==1 && label){
    const uint32_t seconds=now/1000;
    // Uptime is shown until network/NTP time is integrated.
    lv_label_set_text_fmt(label,"Uptime\n%02lu:%02lu:%02lu",
      (unsigned long)(seconds/3600),(unsigned long)((seconds/60)%60),
      (unsigned long)(seconds%60));
    lv_obj_center(label);
  }else if(mode==2 && dots[0]){
    x+=vx;y+=vy;
    if(x<0||x>216){vx=-vx;x=constrain(x,0,216);}
    if(y<0||y>296){vy=-vy;y=constrain(y,0,296);}
    lv_obj_set_pos(dots[0],x,y);
  }else if(mode==3){
    for(int i=0;i<12;++i)if(dots[i]){
      int xx=lv_obj_get_x(dots[i]), yy=lv_obj_get_y(dots[i])+1+(i%3);
      if(yy>320){yy=0;xx=(xx+53+i*17)%240;}
      lv_obj_set_pos(dots[i],xx,yy);
    }
  }
}
} // namespace Saver
