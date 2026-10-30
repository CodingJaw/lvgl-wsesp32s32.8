#pragma once
// Waveshare ESP32-S3-Touch-LCD-2.8 V2 hardware RTC diagnostic.
// PCF85063ATL is I2C ADDRESS 0x51 on GPIO11/10, sharing the initialized
// MotionWake::imuBus with the QMI8658 (whose fixed address is 0x6B).
// NXP datasheet: CTRL1=0x00, CTRL2=0x01, time registers 0x04..0x0A.
// RTC can restore the ESP32 system clock if its OS flag/date are valid.
// Absolutely NO automatic writes to the RTC or its alarm configuration.
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include "motion_wake.h"
#include "time_prefs.h"
#include "clock_savers.h"

namespace RTCLab {
static constexpr uint8_t ADDRESS=0x51;
static constexpr time_t MIN_VALID_EPOCH=(time_t)1704067200LL; // 2024-01-01
static lv_obj_t *rtcLabel=nullptr,*detailLabel=nullptr,*systemLabel=nullptr;
static lv_obj_t *messageLabel=nullptr,*testConfirm=nullptr;
static bool active=false,ntpPending=false,testAllowed=false;
static bool lastReadOK=false,decodedOK=false,clockValid=false;
static uint8_t ctrl1=0,ctrl2=0,lastRaw[7]={};
static uint8_t lastWireError=0;
static uint32_t readCount=0,readFailures=0,tickCount=0;
static uint32_t lastReadMs=0,ntpStartMs=0,lastAdvanceMs=0;
static uint8_t previousSec=0;
static bool seenSecond=false;
static bool bootClockRestored=false;
static char lastError[110]="No RTC I2C reads yet.";
static String lastMessage="Read-only until a synchronization button is pressed.";

static uint8_t toBCD(uint8_t v){return uint8_t(((v/10)<<4)|(v%10));}
static bool validBCD(uint8_t v){return (v&0x0F)<=9 && ((v>>4)&0x0F)<=9;}
static uint8_t fromBCD(uint8_t v){return uint8_t((v>>4)*10+(v&0x0F));}
static bool leap(int y){return (y%4==0 && (y%100!=0 || y%400==0));}
static int daysInMonth(int y,int mo){
  static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(mo<1 || mo>12)return 0;
  return int(days[mo-1])+(mo==2 && leap(y));
}
static void fail(const char *what,int err=-1){
  snprintf(lastError,sizeof(lastError),"%s (I2C=%d)",what,err);
  Serial.printf("[RTC] %s\n",lastError);
}
static bool readBlock(uint8_t reg,uint8_t *dst,size_t n){
  TwoWire &bus=MotionWake::imuBus; // Already begin(11,10,100000) in Saver::begin()
  bus.beginTransmission(ADDRESS); // CRITICAL: not MotionWake::readRegs(), which targets 0x6B
  bus.write(reg);
  lastWireError=bus.endTransmission(false);
  if(lastWireError!=0){
    fail("RTC 0x51 register pointer write FAILED",lastWireError);
    return false;
  }
  const size_t received=bus.requestFrom(ADDRESS,n,true);
  if(received!=n) {
    while(bus.available())bus.read();
    fail("RTC 0x51 register read incomplete",(int)received);
    return false;
  }
  for(size_t i=0;i<n;i++)dst[i]=bus.read();
  return true;
}
static bool writeBlock(uint8_t reg,const uint8_t *bytes,size_t n){
  TwoWire &bus=MotionWake::imuBus;
  bus.beginTransmission(ADDRESS);
  bus.write(reg);
  for(size_t i=0;i<n;i++)bus.write(bytes[i]);
  lastWireError=bus.endTransmission(true);
  if(lastWireError!=0){
    fail("RTC 0x51 write FAILED",lastWireError);
    return false;
  }
  return true;
}
static bool decode(struct tm &utc){
  memset(&utc,0,sizeof(utc));
  if(!validBCD(lastRaw[0]&0x7F) ||
     !validBCD(lastRaw[1]&0x7F) ||
     !validBCD(lastRaw[3]&0x3F) ||
     !validBCD(lastRaw[5]&0x1F) ||
     !validBCD(lastRaw[6]))return false;
  const int year=2000+fromBCD(lastRaw[6]);
  const int month=fromBCD(lastRaw[5]&0x1F);
  const int day=fromBCD(lastRaw[3]&0x3F);
  const int min=fromBCD(lastRaw[1]&0x7F);
  const int sec=fromBCD(lastRaw[0]&0x7F);
  int hour=0;
  if(ctrl1&0x02){ // 12-hour mode
    const uint8_t rawHour=lastRaw[2]&0x1F;
    if(!validBCD(rawHour))return false;
    const int h12=fromBCD(rawHour);
    if(h12<1 || h12>12)return false;
    hour=(h12%12)+((lastRaw[2]&0x20)?12:0);
  }else{
    const uint8_t rawHour=lastRaw[2]&0x3F;
    if(!validBCD(rawHour))return false;
    hour=fromBCD(rawHour);
  }
  if(year<2024 || year>2099 || month<1 || month>12 ||
     day<1 || day>daysInMonth(year,month) ||
     hour>23 || min>59 || sec>59 ||
     (lastRaw[4]&0x07)>6)return false;
  utc.tm_year=year-1900;
  utc.tm_mon=month-1;
  utc.tm_mday=day;
  utc.tm_hour=hour;
  utc.tm_min=min;
  utc.tm_sec=sec;
  utc.tm_wday=int(lastRaw[4]&7);
  utc.tm_isdst=0;  // The physical RTC is always stored in UTC.
  return true;
}
// Gregorian civil-date -> seconds since 1970-01-01, independent of TZ.
static int64_t epochUTC(const struct tm &u){
  int y=u.tm_year+1900;
  const int m=u.tm_mon+1,day=u.tm_mday;
  y-=(m<=2);
  const int era=(y>=0?y:y-399)/400;
  const unsigned yoe=(unsigned)(y-era*400);
  const int mp=m+(m>2?-3:9);
  const unsigned doy=(153*unsigned(mp)+2)/5+unsigned(day)-1;
  const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
  const int64_t days=(int64_t)era*146097+doe-719468;
  return days*86400LL+int64_t(u.tm_hour)*3600+int64_t(u.tm_min)*60+u.tm_sec;
}
static bool read(){
  ++readCount;
  uint8_t control[2]={};
  if(!readBlock(0x00,control,2) ||
     !readBlock(0x04,lastRaw,sizeof(lastRaw))){
    ++readFailures;
    lastReadOK=decodedOK=clockValid=false;
    return false;
  }
  ctrl1=control[0];ctrl2=control[1];
  lastReadOK=true;
  struct tm t={};
  decodedOK=decode(t);
  // Both STOP (CTRL1 bit5) and oscillator stop (seconds bit7) matter.
  clockValid=decodedOK && !(ctrl1&0x20) && !(lastRaw[0]&0x80);
  if(!decodedOK)snprintf(lastError,sizeof(lastError),"RTC responded but BCD/date is invalid.");
  else if(ctrl1&0x20)snprintf(lastError,sizeof(lastError),"RTC CONTROL1 STOP bit is SET; clock is halted.");
  else if(lastRaw[0]&0x80)snprintf(lastError,sizeof(lastError),"RTC oscillator-stop OS=1: clock accuracy unknown.");
  else snprintf(lastError,sizeof(lastError),"RTC read OK at 0x51.");
  return true;
}
static void notice(const String &msg) {
  lastMessage=msg;
  Serial.printf("[RTC] %s\n",lastMessage.c_str());
  if(messageLabel)lv_label_set_text(messageLabel,lastMessage.c_str());
}
static bool writeUTC(const struct tm &t) {
  if(t.tm_year+1900<2024 || t.tm_year+1900>2099 ||
     t.tm_mon<0 || t.tm_mon>11 || t.tm_mday<1 ||
     t.tm_mday>daysInMonth(t.tm_year+1900,t.tm_mon+1) ||
     t.tm_hour<0 || t.tm_hour>23 || t.tm_min<0 || t.tm_min>59 ||
     t.tm_sec<0 || t.tm_sec>59 || t.tm_wday<0 || t.tm_wday>6){
    notice("Refused invalid UTC date/time; RTC unchanged.");
    return false;
  }
  uint8_t oldCtrl=0;
  if(!readBlock(0x00,&oldCtrl,1)){
    notice("RTC not responding at 0x51; no write attempted.");
    return false;
  }
  // Explicit sync sets 24h mode and enables counting; keep existing
  // alarm/clockout/capacitor configuration unchanged.
  if(oldCtrl & (0x20|0x02)) {
    const uint8_t newCtrl=oldCtrl & uint8_t(~0x22);
    if(!writeBlock(0x00,&newCtrl,1)){
      notice("Failed to start RTC / switch to 24h mode.");
      return false;
    }
  }
  const uint8_t payload[7]={
    toBCD(uint8_t(t.tm_sec)),toBCD(uint8_t(t.tm_min)),
    toBCD(uint8_t(t.tm_hour)),toBCD(uint8_t(t.tm_mday)),
    uint8_t(t.tm_wday),toBCD(uint8_t(t.tm_mon+1)),
    toBCD(uint8_t(t.tm_year+1900-2000))
  };
  if(!writeBlock(0x04,payload,sizeof(payload))){
    notice("RTC write FAILED; verify I2C connection.");
    return false;
  }
  if(!read()){
    notice("RTC write sent, but 0x51 verification READ failed.");
    return false;
  }
  struct tm check={};
  const bool verified=clockValid && decode(check);
  const int64_t errorSeconds=verified ? (epochUTC(t)-epochUTC(check)) : 9999LL;
  if(!verified || errorSeconds>1 || errorSeconds< -1){
    notice(String("RTC verify FAILED. ")+lastError);
    return false;
  }
  seenSecond=false;
  tickCount=0;
  lastAdvanceMs=millis();
  notice("RTC write verified at 0x51. Watch seconds advance.");
  return true;
}
static bool restoreSystemFromRTC(bool boot=false) {
  if(!read() || !clockValid){
    if(!boot)notice(String("Cannot restore system: ")+lastError);
    return false;
  }
  struct tm utc={};
  if(!decode(utc))return false;
  const int64_t stamp=epochUTC(utc);
  if(stamp<1704067200LL || stamp>4102444800LL)return false;
  if(boot && time(nullptr)>=MIN_VALID_EPOCH)return false;
  struct timeval tv={};
  tv.tv_sec=(time_t)stamp;
  tv.tv_usec=0;
  if(settimeofday(&tv,nullptr)!=0){
    if(!boot)notice("RTC read valid, but settimeofday() FAILED.");
    return false;
  }
  if(boot) {
    bootClockRestored=true;
    Serial.printf("[RTC] Restored ESP32 system UTC from PCF85063 0x51: %lld\n",
                  (long long)stamp);
  }else notice("ESP32 SYSTEM clock restored from RTC 0x51 (UTC).");
  return true;
}
static void begin() {
  // Called AFTER Saver::begin() initializes the shared GPIO11/10 I2C bus.
  // Offline time restoration is read-only to the RTC and is safe after
  // power-on or motion-deep-sleep wake.
  (void)restoreSystemFromRTC(true);
}
static void detach(){
  active=false;
  ntpPending=false;
  testAllowed=false;
  rtcLabel=detailLabel=systemLabel=messageLabel=testConfirm=nullptr;
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,lv_event_cb_t cb){
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_size(b,LV_PCT(100),37);
  lv_obj_t *l=lv_label_create(b);
  lv_label_set_text(l,name);
  lv_obj_center(l);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
  return b;
}
static void render(lv_obj_t *parent) {
  detach();
  active=true;
  pinMode(9,INPUT_PULLUP);
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(pane,LV_DIR_NONE);
  lv_obj_set_style_pad_all(pane,3,0);
  lv_obj_set_style_pad_row(pane,3,0);

  rtcLabel=lv_label_create(pane);
  lv_obj_set_width(rtcLabel,LV_PCT(100));
  lv_label_set_text(rtcLabel,"RTC 0x51: checking...");
  lv_obj_set_style_text_color(rtcLabel,lv_color_hex(0x144D80),0);

  detailLabel=lv_label_create(pane);
  lv_obj_set_width(detailLabel,LV_PCT(100));
  lv_label_set_long_mode(detailLabel,LV_LABEL_LONG_WRAP);
  lv_label_set_text(detailLabel,"Waiting for RTC registers...");

  systemLabel=lv_label_create(pane);
  lv_obj_set_width(systemLabel,LV_PCT(100));
  lv_label_set_long_mode(systemLabel,LV_LABEL_LONG_WRAP);
  lv_label_set_text(systemLabel,"System UTC: checking...");

  messageLabel=lv_label_create(pane);
  lv_obj_set_width(messageLabel,LV_PCT(100));
  lv_obj_set_height(messageLabel,44);
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
  lv_obj_set_style_pad_row(controls,7,0);
  lv_obj_set_scroll_dir(controls,LV_DIR_VER);

  button(controls,"Read RTC 0x51 now",[](lv_event_t *){
    lastReadMs=0;
    const bool ok=read();
    notice(ok?(clockValid?"RTC read OK; inspect clock seconds.":String("RTC invalid: ")+lastError):
               String("RTC I2C failure: ")+lastError);
  });
  button(controls,"Request NTP (STA Wi-Fi)",[](lv_event_t *){
    if(WiFi.status()!=WL_CONNECTED){
      notice("No home Wi-Fi STA connection. NTP not available.");
      return;
    }
    ntpPending=true;ntpStartMs=millis();
    TimePrefs::setAutoNtp(true);
    configTzTime(TimePrefs::posix(),"pool.ntp.org","time.google.com");
    ClockSavers::ntpConfigured=true;
    notice("NTP requested; local zone preserved. Monitor SYSTEM UTC.");
  });
  button(controls,"Copy SYSTEM UTC -> RTC",[](lv_event_t *){
    const time_t stamp=time(nullptr);
    struct tm utc={};
    if(stamp<MIN_VALID_EPOCH || !gmtime_r(&stamp,&utc)){
      notice("System clock unset. Request NTP or restore from RTC first.");
      return;
    }
    (void)writeUTC(utc);
    lastReadMs=0;
  });
  button(controls,"Copy RTC UTC -> SYSTEM",[](lv_event_t *){
    (void)restoreSystemFromRTC(false);
    lastReadMs=0;
  });
  testConfirm=lv_checkbox_create(controls);
  lv_checkbox_set_text(testConfirm,"Allow RTC +1 minute TEST");
  lv_obj_add_event_cb(testConfirm,[](lv_event_t *e){
    testAllowed=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(controls,"Advance RTC by 1 minute",[](lv_event_t *){
    if(!testAllowed){notice("Check Allow RTC +1 minute TEST first.");return;}
    if(!read()||!clockValid){notice("RTC time invalid. Set it from SYSTEM UTC first.");return;}
    struct tm current={};
    if(!decode(current)){notice("RTC BCD decode failed.");return;}
    const time_t future=(time_t)(epochUTC(current)+60);
    struct tm next={};
    if(!gmtime_r(&future,&next)){notice("UTC +1 minute conversion failed.");return;}
    testAllowed=false;
    if(testConfirm)lv_obj_remove_state(testConfirm,LV_STATE_CHECKED);
    if(writeUTC(next))notice("TEST PASSED: RTC advanced +1 minute. Sync from SYSTEM UTC to restore.");
    lastReadMs=0;
  });
  lv_obj_t *hint=lv_label_create(controls);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "RTC and SYSTEM are two independent clocks. Times here are UTC.\n"
    "NTP only updates SYSTEM; press Copy SYSTEM -> RTC to write the chip.\n"
    "Use +1 minute TEST only for verification; resync when done.\n"
    "RTC STOP and oscillator flags are reported above.\n"
    "RTC backup supply is needed to retain time with main power removed.\n"
    "No alarm is configured; RTC interrupt GPIO9 is read-only.");
  readCount=readFailures=tickCount=0;
  seenSecond=false;
  lastAdvanceMs=lastReadMs=0;
}
static void tick(uint32_t now) {
  if(!active)return;
  if(ntpPending){
    // configTime() is asynchronous. Never claim NTP was received simply
    // because SYSTEM time was already valid from an earlier RTC restore.
    if((uint32_t)(now-ntpStartMs)>=20000){
      ntpPending=false;
      notice(time(nullptr)>=MIN_VALID_EPOCH
        ?"NTP request window ended. SYSTEM clock valid; fresh sync not verified."
        :"NTP timed out; SYSTEM clock is still unset.");
    }
  }
  if(lastReadMs && (uint32_t)(now-lastReadMs)<800)return;
  lastReadMs=now;
  const bool response=read();
  if(response && decodedOK){
    const uint8_t sec=fromBCD(lastRaw[0]&0x7F);
    if(!seenSecond){
      seenSecond=true;
      previousSec=sec;
      lastAdvanceMs=now;
    }else if(sec!=previousSec){
      previousSec=sec;
      lastAdvanceMs=now;
      tickCount++;
    }
  }
  const char *tickStatus=(response && clockValid)
       ? (tickCount>0 && now-lastAdvanceMs<3500?"RUNNING":
          (seenSecond && now-lastAdvanceMs>=3500?"NOT TICKING":"checking..."))
       : "INVALID";
  if(rtcLabel){
    struct tm u={};
    if(response && decode(u)) {
      lv_label_set_text_fmt(rtcLabel,
        "RTC 0x51  %04d-%02d-%02d\n%02d:%02d:%02d UTC",
        u.tm_year+1900,u.tm_mon+1,u.tm_mday,u.tm_hour,u.tm_min,u.tm_sec);
    }else{
      lv_label_set_text(rtcLabel,"RTC 0x51: read or date decode FAILED");
    }
  }
  if(detailLabel){
    lv_label_set_text_fmt(detailLabel,
      "Tick:%s (%lu)  OS:%d STOP:%d\n"
      "I2C read:%lu fail:%lu | mode:%s\n"
      "IRQ9:%d | CTRL1:%02X CTRL2:%02X",
      tickStatus,(unsigned long)tickCount,
      response?!!(lastRaw[0]&0x80):0,
      response?!!(ctrl1&0x20):0,
      (unsigned long)readCount,(unsigned long)readFailures,
      response && (ctrl1&0x02)?"12h":"24h",
      digitalRead(9),(unsigned)ctrl1,(unsigned)ctrl2);
  }
  if(systemLabel){
    const time_t stamp=time(nullptr);
    struct tm u={};
    if(stamp>=MIN_VALID_EPOCH && gmtime_r(&stamp,&u)){
      struct tm rtcUTC={};
      if(clockValid && decode(rtcUTC)){
        const long long difference=(long long)(epochUTC(rtcUTC)-(int64_t)stamp);
        lv_label_set_text_fmt(systemLabel,
          "SYSTEM %02d:%02d:%02d UTC %s\nRTC - SYSTEM: %+lld seconds",
          u.tm_hour,u.tm_min,u.tm_sec,
          ntpPending?"(NTP requested)":"",difference);
      }else{
        lv_label_set_text_fmt(systemLabel,
          "SYSTEM %02d:%02d:%02d UTC %s",
          u.tm_hour,u.tm_min,u.tm_sec,
          ntpPending?"(NTP requested)":(bootClockRestored?"(RTC at boot)":""));
      }
    }else lv_label_set_text(systemLabel,"SYSTEM time not set (request NTP or copy RTC)");
  }
  if(!response && messageLabel){
    lv_label_set_text_fmt(messageLabel,"%s",lastError);
  }
}
} // namespace RTCLab
