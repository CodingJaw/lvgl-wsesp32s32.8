#pragma once
#include <Preferences.h>
#include <math.h>
#include "classic_savers.h"
#include "motion_wake.h"
namespace Saver {
static Preferences prefs;
static uint8_t mode=0; // 0 black, 1 clock, 2 dot, 3 starfield, 4 lines, 5 bubbles, 6 classic stars
static uint32_t timeoutSec=60;
static uint8_t brightness=128;
static bool motionEnabled=false;
static uint8_t motionSensitivity=1;
static uint32_t motionHoldSec=60;
static uint32_t motionHoldUntil=0;
static bool returnSleepEnabled=false;
static uint8_t returnAngle=1;
static uint32_t returnDelaySec=3;
static bool referenceValid=false, movedAway=false, returnMatch=false;
static int16_t refX=0,refY=0,refZ=0;
static uint32_t matchingSince=0;
static bool closeToReference(){
  if(!referenceValid || !MotionWake::hasSample)return false;
  const float dot=float(refX)*MotionWake::ax+float(refY)*MotionWake::ay+float(refZ)*MotionWake::az;
  const float a2=float(refX)*refX+float(refY)*refY+float(refZ)*refZ;
  const float b2=float(MotionWake::ax)*MotionWake::ax+float(MotionWake::ay)*MotionWake::ay+float(MotionWake::az)*MotionWake::az;
  if(a2<1000000.0f || b2<1000000.0f || dot<=0)return false;
  const float cosLimit[]={0.98480775f,0.93969262f,0.86602540f};
  const float c=cosLimit[returnAngle<3?returnAngle:1];
  return dot*dot>=a2*b2*c*c;
}
static void setReturnSleep(bool on,uint8_t angle,uint32_t seconds){
  returnSleepEnabled=on;
  if(on && !motionEnabled){
    motionEnabled=true;
    prefs.putBool("motion",true);
  }
  MotionWake::configure(motionEnabled||returnSleepEnabled,motionSensitivity);
  returnAngle=angle<3?angle:1;
  returnDelaySec=(seconds==2||seconds==3||seconds==5||seconds==10)?seconds:3;
  prefs.putBool("returnSleep",on);
  prefs.putUChar("returnAngle",returnAngle);
  prefs.putUInt("returnDelay",returnDelaySec);
  matchingSince=0;returnMatch=false;
}

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
  motionEnabled=prefs.getBool("motion",false);
  motionSensitivity=prefs.getUChar("motionLevel",1);
  if(motionSensitivity>2)motionSensitivity=1;
  motionHoldSec=prefs.getUInt("motionHold",60);
  if(motionHoldSec!=30 && motionHoldSec!=60 && motionHoldSec!=120 && motionHoldSec!=300)motionHoldSec=60;
  returnSleepEnabled=prefs.getBool("returnSleep",false);
  returnAngle=prefs.getUChar("returnAngle",1);
  if(returnAngle>2)returnAngle=1;
  returnDelaySec=prefs.getUInt("returnDelay",3);
  if(returnDelaySec!=2&&returnDelaySec!=3&&returnDelaySec!=5&&returnDelaySec!=10)returnDelaySec=3;
  MotionWake::begin();
  MotionWake::configure(motionEnabled||returnSleepEnabled,motionSensitivity);
  lastActivity=millis();
}
static void setMotion(bool on,uint8_t level) {
  motionEnabled=on;
  motionSensitivity=level<3?level:1;
  prefs.putBool("motion",motionEnabled);
  prefs.putUChar("motionLevel",motionSensitivity);
  MotionWake::configure(motionEnabled||returnSleepEnabled,motionSensitivity);
  if(!on){motionHoldUntil=0;matchingSince=0;}
}
static void setMotionHold(uint32_t seconds){
  if(seconds!=30 && seconds!=60 && seconds!=120 && seconds!=300)return;
  motionHoldSec=seconds;
  prefs.putUInt("motionHold",seconds);
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
  matchingSince=0;
}
static void start(){
  if(active)return;
  if(returnSleepEnabled && MotionWake::ready && MotionWake::hasSample){
    refX=MotionWake::ax;refY=MotionWake::ay;refZ=MotionWake::az;
    referenceValid=true;movedAway=false;
  }
  matchingSince=0;returnMatch=false;
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
  const bool movementEvent=MotionWake::poll();
  if(motionEnabled && movementEvent) {
    // Every qualifying movement extends the pickup grace period.
    motionHoldUntil=now+motionHoldSec*1000UL;
    if(active && wakeArmed)wakeRequested=true;
    else if(!active)activity();
  }
  if(wakeRequested)stop();
  if(returnSleepEnabled && referenceValid && !active && MotionWake::fresh){
    const bool similar=closeToReference();
    if(!similar)movedAway=true;
    const bool settled=MotionWake::deltaMagnitude<220;
    returnMatch=similar && settled;
    if(movedAway && returnMatch && !touching && !inhibit){
      if(!matchingSince)matchingSince=now;
      else if((uint32_t)(now-matchingSince)>=returnDelaySec*1000UL){
        motionHoldUntil=0;
        start();
        return;
      }
    }else matchingSince=0;
  }
  if(touching)activity();
  if(inhibit){activity();return;}
  if(!active){
    const bool motionHoldActive=motionEnabled && (int32_t)(motionHoldUntil-now)>0;
    if(timeoutSec && !motionHoldActive && !touching && !suppressTouch &&
       (uint32_t)(now-lastActivity)>=timeoutSec*1000UL)start();
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
