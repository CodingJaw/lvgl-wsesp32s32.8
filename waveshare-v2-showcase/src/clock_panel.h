#pragma once
// Clock settings panel: selects a persistent POSIX timezone and supports
// explicit MANUAL LOCAL date/time entry. RTC/ESP32 epochs remain UTC.
// All existing RTC and QMI8658 hardware drivers remain unchanged.
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "time_prefs.h"
#include "rtc_lab.h"
#include "clock_savers.h"

namespace ClockPanel {
static bool active=false;
static lv_obj_t *localLabel=nullptr,*utcLabel=nullptr,*rtcLabel=nullptr;
static lv_obj_t *messageLabel=nullptr,*ntpSwitch=nullptr;
static lv_obj_t *fields[6]={};
static uint32_t lastUiMs=0;
static String lastMessage="Select time zone. Local time is for display; RTC remains UTC.";
static constexpr time_t MIN_EPOCH=(time_t)1704067200LL;
// Edit order: year, month, day, hour, minute, second
static const char *const fieldNames[]={"Year","Month","Day","Hour (24h)","Minute","Second"};
static const int32_t minimum[]={2024,1,1,0,0,0};
static const int32_t maximum[]={2099,12,31,23,59,59};
static const int32_t defaults[]={2026,1,1,12,0,0};

static void note(const String &msg) {
  lastMessage=msg;
  Serial.printf("[CLOCK] %s\n",msg.c_str());
  if(messageLabel)lv_label_set_text(messageLabel,lastMessage.c_str());
}
static bool validSystemTime() {
  return time(nullptr)>=MIN_EPOCH;
}
static void setEditor(const struct tm &t){
  const int v[]={t.tm_year+1900,t.tm_mon+1,t.tm_mday,
                 t.tm_hour,t.tm_min,t.tm_sec};
  for(int i=0;i<6;i++)if(fields[i])lv_spinbox_set_value(fields[i],v[i]);
}
static void loadEditor(){
  time_t stamp=time(nullptr);
  struct tm t={};
  if(stamp>=MIN_EPOCH && localtime_r(&stamp,&t)){
    setEditor(t);
    note("Manual editor loaded from current LOCAL system time.");
    return;
  }
  if(RTCLab::read() && RTCLab::clockValid){
    struct tm rtcUTC={};
    if(RTCLab::decode(rtcUTC)){
      const time_t seconds=(time_t)RTCLab::epochUTC(rtcUTC);
      if(localtime_r(&seconds,&t)){
        setEditor(t);
        note("Manual editor loaded from LOCAL conversion of RTC.");
        return;
      }
    }
  }
  for(int i=0;i<6;i++)if(fields[i])lv_spinbox_set_value(fields[i],defaults[i]);
  note("Clocks unset. Editor begins at 2026-01-01 12:00; adjust manually.");
}
static bool localEditorEpoch(time_t &epoch,struct tm &local){
  for(int i=0;i<6;i++)if(!fields[i]){
    note("Manual date/time editor is unavailable.");
    return false;
  }
  const int yr=lv_spinbox_get_value(fields[0]);
  const int mo=lv_spinbox_get_value(fields[1]);
  const int day=lv_spinbox_get_value(fields[2]);
  const int hour=lv_spinbox_get_value(fields[3]);
  const int minute=lv_spinbox_get_value(fields[4]);
  const int sec=lv_spinbox_get_value(fields[5]);
  // Reject invalid calendar dates before conversion (e.g. February 30).
  if(yr<2024||yr>2099||mo<1||mo>12||day<1||
     day>RTCLab::daysInMonth(yr,mo)||
     hour<0||hour>23||minute<0||minute>59||sec<0||sec>59){
    note("Invalid date/time. Check day of month, hour and seconds.");
    return false;
  }
  struct tm candidate={};
  candidate.tm_year=yr-1900;candidate.tm_mon=mo-1;candidate.tm_mday=day;
  candidate.tm_hour=hour;candidate.tm_min=minute;candidate.tm_sec=sec;
  candidate.tm_isdst=-1;  // Let selected time zone resolve daylight saving.
  const time_t stamp=mktime(&candidate); // LOCAL fields -> UTC epoch
  if(stamp<MIN_EPOCH){
    note("Local time converts to an unsupported UTC date.");
    return false;
  }
  struct tm converted={};
  if(!localtime_r(&stamp,&converted)||
     converted.tm_year!=yr-1900||converted.tm_mon!=mo-1||
     converted.tm_mday!=day||converted.tm_hour!=hour||
     converted.tm_min!=minute||converted.tm_sec!=sec){
    note("This local time does not exist (DST spring-forward gap).");
    return false;
  }
  epoch=stamp;local=converted;
  return true;
}
static void manualSet(bool updateRTC){
  time_t epoch=0;
  struct tm local={};
  if(!localEditorEpoch(epoch,local))return;
  struct timeval tv={};
  tv.tv_sec=epoch;tv.tv_usec=0;
  if(settimeofday(&tv,nullptr)!=0){
    note("settimeofday() failed. Neither clock was changed.");
    return;
  }
  // Disable SNTP so it will not silently overwrite the manual change.
  TimePrefs::setAutoNtp(false);
  ClockSavers::ntpConfigured=false;
  if(ntpSwitch)lv_obj_remove_state(ntpSwitch,LV_STATE_CHECKED);

  char stamp[45]={};
  strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S %Z",&local);
  if(!updateRTC){
    note(String("SYSTEM manually set to ")+stamp+
      ". Auto NTP is OFF; RTC was not changed.");
    return;
  }
  struct tm utc={};
  if(!gmtime_r(&epoch,&utc)){
    note("SYSTEM was set; UTC conversion failed, RTC unchanged.");
    return;
  }
  if(RTCLab::writeUTC(utc)){
    note(String("SYSTEM and RTC set from LOCAL ")+stamp+
         ". NTP OFF; clocks store UTC.");
  }else{
    note(String("SYSTEM set to ")+stamp+
         "; RTC sync FAILED (check RTC page).");
  }
}
static void syncSystemToRTC(){
  const time_t stamp=time(nullptr);
  struct tm u={};
  if(stamp<MIN_EPOCH || !gmtime_r(&stamp,&u)){
    note("No valid SYSTEM UTC; cannot synchronize RTC.");
    return;
  }
  if(RTCLab::writeUTC(u))
    note("RTC 0x51 synchronized from SYSTEM UTC.");
  else
    note("RTC sync FAILED; check RTC page diagnostics.");
}
static void requestNTP(){
  if(WiFi.status()!=WL_CONNECTED){
    note("Home Wi-Fi (STA) must be connected to request NTP.");
    return;
  }
  TimePrefs::setAutoNtp(true);
  if(ntpSwitch)lv_obj_add_state(ntpSwitch,LV_STATE_CHECKED);
  configTzTime(TimePrefs::posix(),"pool.ntp.org","time.google.com");
  ClockSavers::ntpConfigured=true;
  note("NTP requested. SYSTEM updates asynchronously; use Sync RTC after.");
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,lv_event_cb_t callback){
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_size(b,LV_PCT(100),37);
  lv_obj_t *l=lv_label_create(b);
  lv_label_set_text(l,name);
  lv_obj_center(l);
  lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,nullptr);
  return b;
}
static void adjust(lv_event_t *e){
  lv_obj_t *spin=(lv_obj_t *)lv_event_get_user_data(e);
  if(!spin)return;
  // Preserve the tapped digit selection (ones/tens/hundreds).
  if(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))) == 1)
    lv_spinbox_increment(spin);
  else lv_spinbox_decrement(spin);
}
static lv_obj_t *field(lv_obj_t *parent,int index){
  lv_obj_t *cell=lv_obj_create(parent);
  lv_obj_set_width(cell,LV_PCT(47));
  lv_obj_set_height(cell,112);
  lv_obj_set_flex_flow(cell,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cell,3,0);
  lv_obj_set_style_pad_row(cell,3,0);
  lv_obj_set_scroll_dir(cell,LV_DIR_NONE);
  lv_obj_t *title=lv_label_create(cell);
  lv_label_set_text(title,fieldNames[index]);
  lv_obj_set_width(title,LV_PCT(100));
  lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_t *spin=lv_spinbox_create(cell);
  lv_obj_set_width(spin,LV_PCT(100));
  lv_obj_set_height(spin,30);
  lv_spinbox_set_range(spin,minimum[index],maximum[index]);
  lv_spinbox_set_digit_format(spin,index==0?4:2,0);
  lv_spinbox_set_step(spin,1);
  lv_spinbox_set_value(spin,defaults[index]);
  lv_obj_set_style_pad_all(spin,3,0);
  lv_obj_t *row=lv_obj_create(cell);
  lv_obj_set_width(row,LV_PCT(100));
  lv_obj_set_height(row,34);
  lv_obj_set_style_pad_all(row,1,0);
  lv_obj_set_style_pad_column(row,4,0);
  lv_obj_set_style_border_width(row,0,0);
  lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
  lv_obj_set_scroll_dir(row,LV_DIR_NONE);
  for(int direction=-1;direction<=1;direction+=2){
    lv_obj_t *b=lv_button_create(row);
    lv_obj_set_width(b,LV_PCT(47));
    lv_obj_set_height(b,29);
    // LVGL 9: event and object user-data are independent.
    lv_obj_set_user_data(b,(void *)(intptr_t)(direction>0?1:0));
    lv_obj_add_event_cb(b,adjust,LV_EVENT_CLICKED,spin);
    lv_obj_t *t=lv_label_create(b);
    lv_label_set_text(t,direction>0?"+":"-");
    lv_obj_center(t);
  }
  return spin;
}
static void detach(){
  active=false;
  localLabel=utcLabel=rtcLabel=messageLabel=ntpSwitch=nullptr;
  for(auto &entry:fields)entry=nullptr;
}
static void render(lv_obj_t *parent){
  detach();
  active=true;
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(pane,LV_DIR_NONE);
  lv_obj_set_style_pad_all(pane,3,0);
  lv_obj_set_style_pad_row(pane,3,0);

  localLabel=lv_label_create(pane);
  lv_obj_set_width(localLabel,LV_PCT(100));
  lv_obj_set_style_text_color(localLabel,lv_color_hex(0x14558B),0);
  lv_label_set_text(localLabel,"Local clock starting...");
  utcLabel=lv_label_create(pane);
  lv_obj_set_width(utcLabel,LV_PCT(100));
  lv_label_set_text(utcLabel,"SYSTEM UTC...");
  rtcLabel=lv_label_create(pane);
  lv_obj_set_width(rtcLabel,LV_PCT(100));
  lv_label_set_text(rtcLabel,"RTC UTC (PCF85063)...");
  messageLabel=lv_label_create(pane);
  lv_obj_set_width(messageLabel,LV_PCT(100));
  lv_obj_set_height(messageLabel,46);
  lv_label_set_long_mode(messageLabel,LV_LABEL_LONG_WRAP);
  lv_obj_set_style_bg_color(messageLabel,lv_color_hex(0xE1EFFB),0);
  lv_obj_set_style_bg_opa(messageLabel,LV_OPA_COVER,0);
  lv_obj_set_style_text_color(messageLabel,lv_color_hex(0x133454),0);
  lv_obj_set_style_pad_all(messageLabel,3,0);
  lv_label_set_text(messageLabel,lastMessage.c_str());

  lv_obj_t *controls=lv_obj_create(pane);
  lv_obj_set_width(controls,LV_PCT(100));
  lv_obj_set_flex_grow(controls,1);
  lv_obj_set_flex_flow(controls,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(controls,3,0);
  lv_obj_set_style_pad_row(controls,8,0);
  lv_obj_set_scroll_dir(controls,LV_DIR_VER);

  lv_obj_t *tzTitle=lv_label_create(controls);
  lv_label_set_text(tzTitle,"Display time zone (saved)");
  lv_obj_t *zone=lv_dropdown_create(controls);
  lv_obj_set_width(zone,LV_PCT(100));
  String opts=TimePrefs::options();
  lv_dropdown_set_options(zone,opts.c_str());
  lv_dropdown_set_selected(zone,TimePrefs::selected);
  lv_obj_add_event_cb(zone,[](lv_event_t *e){
    const uint16_t chosen=lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if(TimePrefs::choose((uint8_t)chosen)){
      loadEditor();
      note(String("Time zone saved: ")+TimePrefs::name()+
        ". Clock displays now use this zone.");
    }
  },LV_EVENT_VALUE_CHANGED,nullptr);
  ntpSwitch=lv_checkbox_create(controls);
  lv_checkbox_set_text(ntpSwitch,"Automatic NTP on Wi-Fi");
  if(TimePrefs::autoNtp)lv_obj_add_state(ntpSwitch,LV_STATE_CHECKED);
  lv_obj_add_event_cb(ntpSwitch,[](lv_event_t *e){
    const bool enabled=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    TimePrefs::setAutoNtp(enabled);
    ClockSavers::ntpConfigured=false;
    note(enabled?"Auto NTP ON: will sync on Wi-Fi.":"Manual/RTC mode: NTP stopped.");
  },LV_EVENT_VALUE_CHANGED,nullptr);

  button(controls,"Request NTP now",[](lv_event_t *){requestNTP();});
  button(controls,"Sync SYSTEM UTC -> RTC",[](lv_event_t *){syncSystemToRTC();});

  lv_obj_t *editTitle=lv_label_create(controls);
  lv_label_set_text(editTitle,"Manual LOCAL date and time (24h)");
  button(controls,"Load current time into editor",[](lv_event_t *){loadEditor();});

  lv_obj_t *fieldGrid=lv_obj_create(controls);
  lv_obj_set_width(fieldGrid,LV_PCT(100));
  lv_obj_set_height(fieldGrid,LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(fieldGrid,LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all(fieldGrid,2,0);
  lv_obj_set_style_pad_row(fieldGrid,5,0);
  lv_obj_set_style_pad_column(fieldGrid,5,0);
  lv_obj_set_style_border_width(fieldGrid,0,0);
  lv_obj_set_style_bg_opa(fieldGrid,LV_OPA_TRANSP,0);
  lv_obj_set_scroll_dir(fieldGrid,LV_DIR_NONE);
  for(int i=0;i<6;i++)fields[i]=field(fieldGrid,i);

  button(controls,"Set SYSTEM + RTC from editor",[](lv_event_t *){manualSet(true);});
  button(controls,"Set SYSTEM only (RTC unchanged)",[](lv_event_t *){manualSet(false);});
  lv_obj_t *hint=lv_label_create(controls);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "Tap a digit, then +/- (ones/tens). Calendar and DST gaps checked.\n"
    "Manual Set turns automatic NTP OFF to prevent overwrites.\n"
    "The RTC always stores UTC; local time and daylight saving are display conversions.\n"
    "A duplicated hour when clocks go back selects the system's default occurrence.\n"
    "Return to RTC page for low-level clock diagnostics.");
  lastUiMs=0;
  loadEditor();
}
static void tick(uint32_t ms){
  if(!active)return;
  if(lastUiMs && (uint32_t)(ms-lastUiMs)<1000)return;
  lastUiMs=ms;
  const time_t stamp=time(nullptr);
  struct tm local={},utc={};
  if(stamp>=MIN_EPOCH && localtime_r(&stamp,&local) && gmtime_r(&stamp,&utc)){
    char localText[72]={},utcText[64]={};
    strftime(localText,sizeof(localText),"LOCAL %Y-%m-%d\n%I:%M:%S %p %Z",&local);
    strftime(utcText,sizeof(utcText),"SYS UTC %Y-%m-%d %H:%M:%S",&utc);
    if(localLabel)lv_label_set_text(localLabel,localText);
    if(utcLabel)lv_label_set_text(utcLabel,utcText);
  }else{
    if(localLabel)lv_label_set_text(localLabel,"LOCAL: not set (use manual editor)");
    if(utcLabel)lv_label_set_text(utcLabel,"SYSTEM UTC: not set");
  }
  // Read only on this page, using correct 0x51 I2C address.
  const bool ok=RTCLab::read() && RTCLab::clockValid;
  struct tm rtcUTC={};
  if(rtcLabel){
    if(ok && RTCLab::decode(rtcUTC)){
      char buff[60]={};
      strftime(buff,sizeof(buff),"RTC UTC %Y-%m-%d %H:%M:%S",&rtcUTC);
      lv_label_set_text(rtcLabel,buff);
    }else{
      lv_label_set_text(rtcLabel,"RTC UTC: unavailable / oscillator stopped");
    }
  }
}
} // namespace ClockPanel
