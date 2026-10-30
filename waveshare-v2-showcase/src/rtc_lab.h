#pragma once
// PCF85063ATL RTC test. Datasheet registers 0x04..0x0A are UTC-time counters.
// Shared I2C bus is initialized by MotionWake::begin() (GPIO11 SDA, GPIO10 SCL).
// Reading does not alter alarms, clock, oscillator, or QMI8658 wake settings.
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>
#include "motion_wake.h"

namespace RTCLab {
static constexpr uint8_t ADDRESS=0x51;
static lv_obj_t *status=nullptr,*detail=nullptr,*message=nullptr;
static bool active=false,ntpPending=false;
static uint32_t lastRead=0,ntpStart=0;
static uint8_t lastRaw[7]={};
static bool lastValid=false;
static uint8_t bcd(uint8_t n){return uint8_t(((n/10)<<4)|(n%10));}
static uint8_t dec(uint8_t n){return uint8_t((n>>4)*10+(n&15));}

static bool read() {
  uint8_t bytes[7]={};
  if(!MotionWake::readRegs(0x04,bytes,7)) {
    lastValid=false;
    return false;
  }
  memcpy(lastRaw,bytes,7);
  const int sec=dec(bytes[0]&0x7F),min=dec(bytes[1]&0x7F);
  const int hour=dec(bytes[2]&0x3F),day=dec(bytes[3]&0x3F);
  const int weekday=dec(bytes[4]&7),month=dec(bytes[5]&0x1F);
  lastValid=!(bytes[0]&0x80) && sec<60 && min<60 && hour<24 &&
            day>0 && day<=31 && month>0 && month<=12 && weekday<=6;
  return true;
}
static void note(const String &s) {
  Serial.printf("[RTC] %s\n",s.c_str());
  if(message)lv_label_set_text(message,s.c_str());
}
static bool setUTC(const struct tm &u) {
  if(u.tm_year+1900<2024 || u.tm_year+1900>2099) {
    note("System clock is not valid; RTC was not changed.");
    return false;
  }
  // One transaction writes the seven counters; avoid altering controls
  // or alarm registers. PCF85063 uses 00..99 as year offset from 2000.
  TwoWire &bus=MotionWake::imuBus;
  bus.beginTransmission(ADDRESS);
  bus.write(0x04);
  bus.write(bcd(uint8_t(u.tm_sec)));
  bus.write(bcd(uint8_t(u.tm_min)));
  bus.write(bcd(uint8_t(u.tm_hour)));
  bus.write(bcd(uint8_t(u.tm_mday)));
  bus.write(bcd(uint8_t(u.tm_wday)));
  bus.write(bcd(uint8_t(u.tm_mon+1)));
  bus.write(bcd(uint8_t(u.tm_year+1900-2000)));
  if(bus.endTransmission()!=0){
    note("RTC write FAILED (I2C).");
    return false;
  }
  const bool ok=read() && lastValid;
  note(ok?"RTC synchronized from UTC system time.":"RTC write sent but verify FAILED.");
  return ok;
}
static lv_obj_t *action(lv_obj_t *parent,const char *name,lv_event_cb_t cb) {
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_size(b,LV_PCT(100),38);
  lv_obj_t *l=lv_label_create(b);
  lv_label_set_text(l,name);
  lv_obj_center(l);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
  return b;
}
static void detach() {
  active=false;
  ntpPending=false;
  status=detail=message=nullptr;
}
static void render(lv_obj_t *parent) {
  detach();
  active=true;
  lv_obj_set_scroll_dir(parent,LV_DIR_VER);
  lv_obj_t *container=lv_obj_create(parent);
  lv_obj_set_size(container,LV_PCT(100),LV_PCT(100));
  lv_obj_set_flex_flow(container,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(container,5,0);
  lv_obj_set_style_pad_row(container,9,0);
  lv_obj_set_scroll_dir(container,LV_DIR_VER);
  lv_obj_t *heading=lv_label_create(container);
  lv_label_set_text(heading,"RTC - PCF85063ATL");
  status=lv_label_create(container);
  lv_obj_set_width(status,LV_PCT(100));
  lv_label_set_text(status,"Checking chip at I2C 0x51...");
  detail=lv_label_create(container);
  lv_obj_set_width(detail,LV_PCT(100));
  lv_label_set_text(detail,"Date, oscillator flag and IRQ9");
  message=lv_label_create(container);
  lv_obj_set_width(message,LV_PCT(100));
  lv_label_set_long_mode(message,LV_LABEL_LONG_WRAP);
  lv_label_set_text(message,"RTC read-only until Sync is pressed.");
  action(container,"Refresh RTC",[](lv_event_t *){lastRead=0;});
  action(container,"Request NTP (Wi-Fi)",[](lv_event_t *){
    if(WiFi.status()!=WL_CONNECTED){
      note("STA Wi-Fi not connected. NTP cannot start.");
      return;
    }
    configTime(0,0,"pool.ntp.org","time.google.com");
    ntpPending=true;
    ntpStart=millis();
    note("NTP requested. Waiting for valid UTC system time...");
  });
  action(container,"Sync RTC from SYSTEM UTC",[](lv_event_t *){
    time_t now=time(nullptr);
    struct tm utc={};
    if(now<1704067200LL || !gmtime_r(&now,&utc)){
      note("No valid system clock. Request NTP first.");
      return;
    }
    setUTC(utc);
    lastRead=0;
  });
  lv_obj_t *hint=lv_label_create(container);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "RTC displays UTC (not local timezone).\n"
    "Read test is non-destructive.\n"
    "Backup battery retention: set RTC, disconnect main power, then recheck.\n"
    "GPIO9 is the RTC interrupt; no alarm is armed in this test.");
  lastRead=0;
}
static void tick(uint32_t now) {
  if(!active)return;
  if(ntpPending) {
    const time_t t=time(nullptr);
    if(t>=1704067200LL){
      ntpPending=false;
      note("System UTC is valid. Press Sync RTC to write.");
    }else if((uint32_t)(now-ntpStart)>=15000) {
      ntpPending=false;
      note("NTP timed out after 15 seconds; RTC unchanged.");
    }
  }
  if(lastRead && now-lastRead<1000)return;
  lastRead=now;
  if(!read()){
    if(status)lv_label_set_text(status,"NOT FOUND / I2C read error");
    if(detail)lv_label_set_text(detail,"SDA=11 SCL=10, addr 0x51");
    return;
  }
  if(status){
    lv_label_set_text_fmt(status,
      "%04d-%02u-%02u\n%02u:%02u:%02u UTC",
      2000+dec(lastRaw[6]),unsigned(dec(lastRaw[5]&0x1F)),
      unsigned(dec(lastRaw[3]&0x3F)),
      unsigned(dec(lastRaw[2]&0x3F)),
      unsigned(dec(lastRaw[1]&0x7F)),
      unsigned(dec(lastRaw[0]&0x7F)));
  }
  if(detail){
    lv_label_set_text_fmt(detail,
      "Chip: 0x51 | Clock: %s\nOscillator-stop bit: %d\n"
      "IRQ GPIO9: %s\nWeekday: %u (0=Sun)\n"
      "System UTC synced: %s",
      lastValid?"valid":"NOT VALID",!!(lastRaw[0]&0x80),
      digitalRead(9)?"HIGH":"LOW",unsigned(dec(lastRaw[4]&7)),
      time(nullptr)>=1704067200LL?"yes":"no");
  }
}
} // namespace RTCLab
