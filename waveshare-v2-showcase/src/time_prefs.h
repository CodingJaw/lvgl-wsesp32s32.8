#pragma once
// Clock display-zone configuration. The ESP32 system epoch and PCF85063 remain UTC.
// Only localtime()/mktime()/strftime() presentation uses this saved POSIX zone.
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <stdlib.h>
#include <esp_sntp.h>

namespace TimePrefs {
static Preferences prefs;
static bool initialized=false;
static uint8_t selected=1; // Existing showcase behavior: US Eastern with DST.
static bool autoNtp=true; // Manual setting turns this off to prevent overwrite.
struct Zone {const char *name; const char *posix;};
static constexpr Zone zones[]={
  {"UTC", "UTC0"},
  {"US Eastern (EST/EDT)", "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"US Central (CST/CDT)", "CST6CDT,M3.2.0/2,M11.1.0/2"},
  {"US Mountain (MST/MDT)", "MST7MDT,M3.2.0/2,M11.1.0/2"},
  {"Arizona (MST no DST)", "MST7"},
  {"US Pacific (PST/PDT)", "PST8PDT,M3.2.0/2,M11.1.0/2"},
  {"Alaska (AKST/AKDT)", "AKST9AKDT,M3.2.0/2,M11.1.0/2"},
  {"Hawaii (HST)", "HST10"},
  {"London (GMT/BST)", "GMT0BST,M3.5.0/1,M10.5.0/2"},
  {"Central Europe (CET/CEST)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"India (IST)", "IST-5:30"},
  {"Japan (JST)", "JST-9"},
  {"Sydney (AEST/AEDT)", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"}
};
static constexpr uint8_t COUNT=sizeof(zones)/sizeof(zones[0]);
static void apply() {
  setenv("TZ",zones[selected].posix,1);
  tzset();
  Serial.printf("[CLOCK] local zone=%s POSIX=%s\n",zones[selected].name,zones[selected].posix);
}
static void begin() {
  if(initialized)return;
  prefs.begin("clock-config",false);
  selected=prefs.getUChar("tz",1);
  if(selected>=COUNT)selected=1;
  autoNtp=prefs.getBool("ntp",true);
  initialized=true;
  apply();
}
static bool choose(uint8_t index) {
  if(index>=COUNT)return false;
  selected=index;
  if(initialized)prefs.putUChar("tz",index);
  apply(); // No change to RTC UTC or system epoch.
  return true;
}
static void setAutoNtp(bool on) {
  autoNtp=on;
  if(initialized)prefs.putBool("ntp",on);
  // Unlike simply disabling future configTime(), this also stops an
  // already-running SNTP client so manual time is not overwritten.
  if(!on && esp_sntp_enabled())esp_sntp_stop();
  Serial.printf("[CLOCK] automatic NTP: %s\n",on?"ON":"OFF");
}
static const char *name(){return zones[selected].name;}
static const char *posix(){return zones[selected].posix;}
static String options() {
  String all;
  for(uint8_t i=0;i<COUNT;i++){if(i)all+="\n";all+=zones[i].name;}
  return all;
}
}
