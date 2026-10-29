#pragma once
// Non-invasive live hardware diagnostics for Waveshare ESP32-S3-Touch-LCD-2.8 V2.
// IMPORTANT: this page does not reconfigure QMI8658 or any sleep/wake GPIO.
// It samples the existing accelerometer configuration (8g / 125Hz).
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <math.h>
#include <esp_system.h>
#include "screensaver.h"
#include "power_tests.h"
#include "wifi_portal.h"

namespace Diagnostics {
static bool active=false, paused=false;
static lv_obj_t *touchLabel=nullptr;
static lv_obj_t *motionLabel=nullptr;
static lv_obj_t *chart=nullptr;
static lv_chart_series_t *motionSeries=nullptr;
static lv_obj_t *performanceLabel=nullptr;
static lv_obj_t *memoryLabel=nullptr;
static lv_obj_t *wifiLabel=nullptr;
static lv_obj_t *powerLabel=nullptr;
static lv_obj_t *pauseText=nullptr;

static uint32_t startedMs=0, lastTickMs=0, lastSlowMs=0;
static uint32_t touchReads=0, touchErrors=0, touchPresses=0;
static uint16_t touchX=0, touchY=0;
static bool touching=false;
static uint32_t loopCount=0, loopPerSecond=0;
static uint32_t flushCountBaseline=0, flushPixelBaseline=0, flushTimeBaseline=0;
static uint32_t flushRate=0, pixelRate=0, avgFlushUs=0, totalFlushMaxUs=0;
static uint32_t imuReads=0, imuErrors=0, peakMotionMg=0;
static int16_t ax=0,ay=0,az=0;
static int32_t motionMg=0;
static bool lastAccelValid=false;
static uint32_t minHeap=0,minPsram=0;

static const char *resetName(esp_reset_reason_t reason) {
  switch(reason) {
    case ESP_RST_POWERON:return "Power-on";
    case ESP_RST_SW:return "Software";
    case ESP_RST_PANIC:return "Crash/panic";
    case ESP_RST_INT_WDT:return "Interrupt WDT";
    case ESP_RST_TASK_WDT:return "Task WDT";
    case ESP_RST_WDT:return "Other WDT";
    case ESP_RST_DEEPSLEEP:return "Deep sleep";
    case ESP_RST_BROWNOUT:return "Brownout";
    default:return "Other";
  }
}
static lv_obj_t *makeCard(lv_obj_t *parent,const char *name) {
  lv_obj_t *box=lv_obj_create(parent);
  lv_obj_set_width(box,LV_PCT(100));
  lv_obj_set_height(box,LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(box,5,0);
  lv_obj_set_style_pad_row(box,4,0);
  lv_obj_remove_flag(box,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *heading=lv_label_create(box);
  lv_label_set_text(heading,name);
  lv_obj_set_style_text_color(heading,lv_color_hex(0x245D93),0);
  lv_obj_set_width(heading,LV_PCT(100));
  return box;
}
static lv_obj_t *makeReading(lv_obj_t *parent) {
  lv_obj_t *l=lv_label_create(parent);
  lv_obj_set_width(l,LV_PCT(100));
  lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
  lv_label_set_text(l,"Collecting readings...");
  return l;
}
static lv_obj_t *miniButton(lv_obj_t *parent,const char *label,
                            lv_event_cb_t handler) {
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_width(b,60);
  lv_obj_set_height(b,31);
  lv_obj_t *t=lv_label_create(b);
  lv_label_set_text(t,label);
  lv_obj_center(t);
  lv_obj_add_event_cb(b,handler,LV_EVENT_CLICKED,nullptr);
  return t;
}
static void resetCounters() {
  touchReads=touchErrors=touchPresses=0;
  imuReads=imuErrors=peakMotionMg=0;
  motionMg=0;
  lastAccelValid=false;
  minHeap=ESP.getFreeHeap();
  minPsram=ESP.getFreePsram();
  startedMs=millis();
  Serial.println("[DIAG] Session counters reset");
}
static void printSnapshot() {
  Serial.printf("[DIAG] uptime=%lus CPU=%uMHz reset=%s; heap=%lu free (session min=%lu, boot min=%lu) PSRAM=%lu free (session min=%lu, boot min=%lu)\n",
    (unsigned long)(millis()/1000),(unsigned)ESP.getCpuFreqMHz(),
    resetName(esp_reset_reason()),(unsigned long)ESP.getFreeHeap(),
    (unsigned long)minHeap,(unsigned long)ESP.getMinFreeHeap(),
    (unsigned long)ESP.getFreePsram(),(unsigned long)minPsram,
    (unsigned long)ESP.getMinFreePsram());
  Serial.printf("[DIAG] touch=%s x=%u y=%u IRQ4=%d samples=%lu errors=%lu presses=%lu\n",
    touching?"DOWN":"UP",(unsigned)touchX,(unsigned)touchY,digitalRead(4),
    (unsigned long)touchReads,(unsigned long)touchErrors,(unsigned long)touchPresses);
  Serial.printf("[DIAG] QMI8658=%s accelRaw=(%d,%d,%d) change=%ldmg peak=%lumg samples=%lu errors=%lu INT1=%d INT2=%d\n",
    MotionWake::ready?"OK":"FAIL",(int)ax,(int)ay,(int)az,(long)motionMg,
    (unsigned long)peakMotionMg,(unsigned long)imuReads,(unsigned long)imuErrors,
    digitalRead(13),digitalRead(12));
  Serial.printf("[DIAG] UI loop/s=%lu LCD flush/s=%lu pixels/s=%lu avgFlush=%luus maxSinceBoot=%luus\n",
    (unsigned long)loopPerSecond,(unsigned long)flushRate,
    (unsigned long)pixelRate,(unsigned long)avgFlushUs,
    (unsigned long)totalFlushMaxUs);
  Serial.printf("[DIAG] BAT~%lumV SYS_EN=%d PWR=%d; AP=%d clients=%d STA=%d RSSI=%lddBm\n",
    (unsigned long)PowerTests::batteryMilliVolts(),digitalRead(7),digitalRead(6),
    WifiPortal::apEnabled,WiFi.softAPgetStationNum(),
    WiFi.status()==WL_CONNECTED,(long)WiFi.RSSI());
}
static void detach() {
  active=false;
  touchLabel=motionLabel=performanceLabel=memoryLabel=wifiLabel=powerLabel=nullptr;
  chart=nullptr;motionSeries=nullptr;pauseText=nullptr;
}
static void render(lv_obj_t *parent,
                   uint32_t flushCalls,uint32_t flushPixels,uint32_t flushMicros) {
  detach();
  active=true;
  paused=false;
  resetCounters();
  lastTickMs=lastSlowMs=millis();
  loopCount=0;
  flushCountBaseline=flushCalls;
  flushPixelBaseline=flushPixels;
  flushTimeBaseline=flushMicros;

  // Scrolling belongs to the child; the navigation bar is untouched.
  lv_obj_set_scroll_dir(parent,LV_DIR_NONE);
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(pane,3,0);
  lv_obj_set_style_pad_row(pane,8,0);
  lv_obj_set_scroll_dir(pane,LV_DIR_VER);

  lv_obj_t *toolbar=lv_obj_create(pane);
  lv_obj_set_width(toolbar,LV_PCT(100));
  lv_obj_set_height(toolbar,LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(toolbar,LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(toolbar,2,0);
  lv_obj_set_style_pad_column(toolbar,3,0);
  lv_obj_remove_flag(toolbar,LV_OBJ_FLAG_SCROLLABLE);

  pauseText=miniButton(toolbar,"Pause",[](lv_event_t *){
    paused=!paused;
    if(pauseText)lv_label_set_text(pauseText,paused?"Resume":"Pause");
    if(!paused){
      lastTickMs=lastSlowMs=millis();
      loopCount=0;
    }
  });
  miniButton(toolbar,"Reset",[](lv_event_t *){
    resetCounters();
    if(chart && motionSeries){
      lv_chart_set_all_value(chart,motionSeries,0);
      lv_chart_refresh(chart);
    }
  });
  miniButton(toolbar,"Serial",[](lv_event_t *){printSnapshot();});

  lv_obj_t *touch=makeCard(pane,"TOUCH | CST3530");
  touchLabel=makeReading(touch);
  lv_obj_t *motion=makeCard(pane,"IMU | QMI8658 accelerometer");
  motionLabel=makeReading(motion);
  chart=lv_chart_create(motion);
  lv_obj_set_size(chart,LV_PCT(100),92);
  lv_chart_set_type(chart,LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart,30);
  lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,1200);
  lv_chart_set_div_line_count(chart,4,5);
  motionSeries=lv_chart_add_series(chart,lv_palette_main(LV_PALETTE_BLUE),
                                    LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t *perf=makeCard(pane,"PERFORMANCE | UI + LCD SPI");
  performanceLabel=makeReading(perf);
  lv_obj_t *mem=makeCard(pane,"MEMORY | ESP32-S3");
  memoryLabel=makeReading(mem);
  lv_obj_t *wifi=makeCard(pane,"NETWORK | Wi-Fi");
  wifiLabel=makeReading(wifi);
  lv_obj_t *power=makeCard(pane,"BATTERY / POWER");
  powerLabel=makeReading(power);
  lv_obj_t *hint=lv_label_create(pane);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "LCD flush/s is not full-frame FPS.\n"
    "IMU gyro stays OFF to conserve power.\n"
    "Battery voltage is approximate; current and true charge % need external hardware.\n"
    "Diagnostics does not change sleep or wake settings.");
}
static void noteTouchSample(bool ok,bool down,uint16_t x,uint16_t y) {
  if(!active)return;
  ++touchReads;
  if(!ok)++touchErrors;
  if(down && !touching)++touchPresses;
  touching=down;
  if(ok){touchX=x;touchY=y;}
}
static void sampleIMU() {
  if(!MotionWake::ready)return;
  uint8_t bytes[6]={};
  if(!MotionWake::readRegs(0x35,bytes,sizeof(bytes))){
    ++imuErrors;
    return;
  }
  const int16_t x=int16_t(uint16_t(bytes[0])|(uint16_t(bytes[1])<<8));
  const int16_t y=int16_t(uint16_t(bytes[2])|(uint16_t(bytes[3])<<8));
  const int16_t z=int16_t(uint16_t(bytes[4])|(uint16_t(bytes[5])<<8));
  if(lastAccelValid){
    const int32_t dx=int32_t(x)-ax,dy=int32_t(y)-ay,dz=int32_t(z)-az;
    const float mag=sqrtf(float(int64_t(dx)*dx+int64_t(dy)*dy+int64_t(dz)*dz));
    motionMg=int32_t(mag*1000.0f/4096.0f);
    if((uint32_t)motionMg>peakMotionMg)peakMotionMg=motionMg;
    if(chart && motionSeries){
      lv_chart_set_next_value(chart,motionSeries,constrain(motionMg,0,1200));
      lv_chart_refresh(chart);
    }
  }
  ax=x;ay=y;az=z;
  lastAccelValid=true;
  ++imuReads;
}
static void tick(uint32_t now,uint32_t flushCalls,uint32_t flushPixels,
                 uint32_t flushMicros,uint32_t flushMaxUs,bool screensaverActive) {
  if(!active || screensaverActive)return;
  ++loopCount;
  if(paused){
    // Keep a fresh counter baseline so Resume does not report a spike
    // caused by graphics work that occurred while the display was paused.
    flushCountBaseline=flushCalls;
    flushPixelBaseline=flushPixels;
    flushTimeBaseline=flushMicros;
    lastTickMs=now;
    loopCount=0;
    return;
  }
  if((uint32_t)(now-lastTickMs)<300)return;
  const uint32_t elapsed=now-lastTickMs;
  loopPerSecond=elapsed?(loopCount*1000UL/elapsed):0;
  loopCount=0;
  lastTickMs=now;
  const uint32_t callDelta=flushCalls-flushCountBaseline;
  const uint32_t pixelsDelta=flushPixels-flushPixelBaseline;
  const uint32_t timeDelta=flushMicros-flushTimeBaseline;
  flushCountBaseline=flushCalls;
  flushPixelBaseline=flushPixels;
  flushTimeBaseline=flushMicros;
  flushRate=elapsed?uint32_t(uint64_t(callDelta)*1000ULL/elapsed):0;
  pixelRate=elapsed?uint32_t(uint64_t(pixelsDelta)*1000ULL/elapsed):0;
  avgFlushUs=callDelta?(timeDelta/callDelta):0;
  totalFlushMaxUs=flushMaxUs;

  sampleIMU();
  if(touchLabel)lv_label_set_text_fmt(touchLabel,
    "x:%u  y:%u  %s | IRQ4:%d\n"
    "Reads:%lu Errors:%lu Presses:%lu",
    (unsigned)touchX,(unsigned)touchY,touching?"DOWN":"UP",digitalRead(4),
    (unsigned long)touchReads,(unsigned long)touchErrors,
    (unsigned long)touchPresses);
  if(motionLabel){
    if(!MotionWake::ready)lv_label_set_text(motionLabel,"QMI8658 not detected");
    else {
      // Original driver uses +/-8g: 4096 raw counts per g.
      const int32_t mgX=int32_t(ax)*1000/4096;
      const int32_t mgY=int32_t(ay)*1000/4096;
      const int32_t mgZ=int32_t(az)*1000/4096;
      const int tilt=int(atan2f(sqrtf(float(ax)*ax+float(ay)*ay),
                                  fabsf(float(az)))*57.29578f);
      lv_label_set_text_fmt(motionLabel,
        "X:%ld Y:%ld Z:%ld mg\n"
        "Tilt:%d deg | Delta:%ld mg\n"
        "Peak:%lu mg | Samples:%lu Err:%lu\n"
        "INT1:%d INT2:%d | Pickup:%s",
        (long)mgX,(long)mgY,(long)mgZ,tilt,(long)motionMg,
        (unsigned long)peakMotionMg,(unsigned long)imuReads,
        (unsigned long)imuErrors,digitalRead(13),digitalRead(12),
        Saver::motionEnabled?"ON":"OFF");
    }
  }
  if(performanceLabel)lv_label_set_text_fmt(performanceLabel,
    "Main loop: %lu/s\n"
    "SPI flush: %lu/s | pixels: %lu/s\n"
    "Avg flush: %lu us | Max: %lu us\n"
    "Flush metrics include this screen",
    (unsigned long)loopPerSecond,(unsigned long)flushRate,
    (unsigned long)pixelRate,(unsigned long)avgFlushUs,
    (unsigned long)totalFlushMaxUs);
  if((uint32_t)(now-lastSlowMs)<1000)return;
  lastSlowMs=now;
  const uint32_t heap=ESP.getFreeHeap(),psram=ESP.getFreePsram();
  if(!minHeap||heap<minHeap)minHeap=heap;
  if(!minPsram||psram<minPsram)minPsram=psram;
  if(memoryLabel)lv_label_set_text_fmt(memoryLabel,
    "CPU:%u MHz  Core(s):%u\n"
    "Uptime:%luh %lum | Reset:%s\n"
    "Heap:%lu KB free / %lu KB total\n"
    "Min heap session/boot:%lu/%lu KB\n"
    "Largest heap block:%lu KB\n"
    "PSRAM:%lu KB free / %lu KB total\n"
    "Min PSRAM session/boot:%lu/%lu KB\n"
    "Largest PSRAM block:%lu KB",
    (unsigned)ESP.getCpuFreqMHz(),(unsigned)ESP.getChipCores(),
    (unsigned long)(now/3600000UL),(unsigned long)((now/60000UL)%60),
    resetName(esp_reset_reason()),
    (unsigned long)(heap/1024),(unsigned long)(ESP.getHeapSize()/1024),
    (unsigned long)(minHeap/1024),
    (unsigned long)(ESP.getMinFreeHeap()/1024),
    (unsigned long)(ESP.getMaxAllocHeap()/1024),
    (unsigned long)(psram/1024),(unsigned long)(ESP.getPsramSize()/1024),
    (unsigned long)(minPsram/1024),
    (unsigned long)(ESP.getMinFreePsram()/1024),
    (unsigned long)(ESP.getMaxAllocPsram()/1024));
  if(wifiLabel){
    const bool connected=WiFi.status()==WL_CONNECTED;
    const String apIP=WifiPortal::apEnabled?WiFi.softAPIP().toString():String("off");
    const String staIP=connected?WiFi.localIP().toString():String("not connected");
    lv_label_set_text_fmt(wifiLabel,
      "AP:%s | clients:%d\n"
      "AP IP:%s\n"
      "STA:%s | RSSI:%ld dBm\n"
      "STA IP:%s",
      WifiPortal::apEnabled?"ON":"OFF",WiFi.softAPgetStationNum(),
      apIP.c_str(),connected?"connected":"offline",
      connected?(long)WiFi.RSSI():0L,staIP.c_str());
  }
  if(powerLabel){
    const uint32_t mv=PowerTests::batteryMilliVolts();
    lv_label_set_text_fmt(powerLabel,
      "Battery ADC: ~%lu mV\n"
      "SYS_EN GPIO7:%s  PWR GPIO6:%d\n"
      "Backlight PWM:%u/255\n"
      "Saver:%s  Motion wake:%s\n"
      "Deep wake test: Power tab",
      (unsigned long)mv,digitalRead(7)?"HIGH":"LOW",
      digitalRead(6),(unsigned)Saver::brightness,
      Saver::active?"ACTIVE":"inactive",Saver::motionEnabled?"ON":"OFF");
  }
}
} // namespace Diagnostics
