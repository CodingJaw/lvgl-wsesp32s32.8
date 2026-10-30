#pragma once
#include <time.h>
#include <WiFi.h>
#include "time_prefs.h"
// Clock savers: analog face, 12-hour digital and 24-hour digital.
// NTP is only used when the station has Internet; no invented time while offline.
namespace ClockSavers {
static bool ntpConfigured=false;
static lv_obj_t *face=nullptr,*digital=nullptr,*dateLabel=nullptr,*status=nullptr;
static lv_obj_t *hands[3]={};
static lv_point_precise_t handPoints[3][2]={};
static lv_obj_t *ticks[12]={};
static uint32_t lastSyncCheck=0;
static void service(){
  // RTC and ESP32 system epoch remain UTC. Localtime uses TimePrefs.
  // Don't start NTP in manual/offline mode; manual edits must persist.
  if(!TimePrefs::autoNtp){
    if(ntpConfigured){
      TimePrefs::setAutoNtp(false); // Ensure active SNTP service is stopped.
      ntpConfigured=false;
    }
    return;
  }
  if(WiFi.status()==WL_CONNECTED && !ntpConfigured){
    configTzTime(TimePrefs::posix(),"pool.ntp.org","time.nist.gov");
    ntpConfigured=true;
    Serial.printf("[CLOCK] NTP configured with zone %s\n",TimePrefs::name());
  }
}
static bool current(tm &out){
  time_t t=time(nullptr);
  if(t<1700000000)return false;
  return localtime_r(&t,&out)!=nullptr;
}
static void init(lv_obj_t *parent,int mode){
  face=nullptr;digital=nullptr;dateLabel=nullptr;status=nullptr;
  for(auto &h:hands)h=nullptr;
  if(mode==4){
    face=lv_obj_create(parent);
    lv_obj_set_size(face,216,216);
    lv_obj_align(face,LV_ALIGN_CENTER,0,-10);
    lv_obj_set_style_radius(face,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(face,lv_color_black(),0);
    lv_obj_set_style_border_color(face,lv_color_white(),0);
    lv_obj_set_style_border_width(face,2,0);
    lv_obj_set_style_pad_all(face,0,0);
    lv_obj_remove_flag(face,LV_OBJ_FLAG_SCROLLABLE);
    for(int i=0;i<12;++i){
      const float a=(i*30-90)*3.14159265f/180.0f;
      ticks[i]=lv_obj_create(face);
      lv_obj_set_size(ticks[i],i%3==0?7:4,i%3==0?7:4);
      lv_obj_set_style_radius(ticks[i],LV_RADIUS_CIRCLE,0);
      lv_obj_set_style_border_width(ticks[i],0,0);
      lv_obj_set_style_pad_all(ticks[i],0,0);
      lv_obj_set_style_bg_color(ticks[i],lv_color_white(),0);
      lv_obj_set_pos(ticks[i],104+int(cosf(a)*90),104+int(sinf(a)*90));
    }
    for(int i=0;i<3;++i){
      hands[i]=lv_line_create(face);
      handPoints[i][0]={107,107};
      handPoints[i][1]={107,107};
      lv_line_set_points(hands[i],handPoints[i],2);
      lv_obj_set_style_line_width(hands[i],i==0?5:(i==1?3:1),0);
      lv_obj_set_style_line_rounded(hands[i],true,0);
      lv_obj_set_style_line_color(hands[i],i==2?lv_color_hex(0xFF5555):lv_color_white(),0);
    }
  }else{
    digital=lv_label_create(parent);
    lv_obj_set_style_text_color(digital,lv_color_white(),0);
    lv_obj_set_style_text_font(digital,&lv_font_montserrat_28,0);
    lv_obj_align(digital,LV_ALIGN_CENTER,0,-24);
  }
  dateLabel=lv_label_create(parent);
  lv_obj_set_style_text_color(dateLabel,lv_color_hex(0xAAAAAA),0);
  lv_obj_align(dateLabel,LV_ALIGN_BOTTOM_MID,0,-18);
  status=lv_label_create(parent);
  lv_obj_set_style_text_color(status,lv_color_hex(0x888888),0);
  lv_obj_align(status,LV_ALIGN_TOP_MID,0,12);
}
static void tick(int mode){
  tm t={};
  if(!current(t)){
    if(status)lv_label_set_text(status,"Clock unset - use Clock panel");
    if(digital)lv_label_set_text(digital,"--:--");
    if(dateLabel)lv_label_set_text(dateLabel,"");
    return;
  }
  if(status)lv_label_set_text(status,"");
  if(dateLabel){
    char buf[40];
    strftime(buf,sizeof(buf),"%a, %b %d, %Y",&t);
    lv_label_set_text(dateLabel,buf);
  }
  if(mode==4){
    const float values[3]={float((t.tm_hour%12)*30)+float(t.tm_min)*0.5f,
                           float(t.tm_min*6)+float(t.tm_sec)*0.1f,float(t.tm_sec*6)};
    const int lengths[3]={52,77,84};
    for(int i=0;i<3;++i){
      float a=(values[i]-90.0f)*3.14159265f/180.0f;
      handPoints[i][1]={int16_t(107+cosf(a)*lengths[i]),int16_t(107+sinf(a)*lengths[i])};
      lv_line_set_points(hands[i],handPoints[i],2);
    }
  }else if(digital){
    char buf[40];
    strftime(buf,sizeof(buf),mode==5?"%I:%M:%S %p":"%H:%M:%S",&t);
    lv_label_set_text(digital,buf);
    lv_obj_align(digital,LV_ALIGN_CENTER,0,-24);
  }
}
}
