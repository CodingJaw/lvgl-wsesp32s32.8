#pragma once
// Power lab for Waveshare ESP32-S3-Touch-LCD-2.8 V2.
// Schematic: QMI8658 INT1=GPIO13, INT2=GPIO12, BAT_ADC=GPIO8,
// SYS_EN/BAT_Control=GPIO7, backlight=GPIO5.
// WoM sequence follows QMI8658C datasheet Rev 0.6 section 9.
// Sleep wakeup requires keeping SYS_EN high. Hardware-off DOES NOT wake on motion.
#include <Arduino.h>
#include <lvgl.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include "screensaver.h"
#include "wifi_portal.h"

namespace PowerTests {
enum Action : uint8_t { NONE, BACKLIGHT_10S, LIGHT_5S, DEEP_TIMER, DEEP_MOTION, HARD_OFF };
static RTC_DATA_ATTR uint32_t testMagic = 0;
static RTC_DATA_ATTR uint8_t lastDeepTest = 0;
static RTC_DATA_ATTR uint8_t lastWomMg = 0;
static constexpr uint32_t MAGIC = 0x50575232UL;
static Action pending = NONE;
static uint32_t scheduledMs = 0;
static uint32_t backlightUntil = 0;
static bool allowHardOff = false;
static uint8_t womThresholdMg = 80;
static lv_obj_t *live = nullptr;
static lv_obj_t *notice = nullptr;
static uint32_t lastUi = 0;
static String bootMessage;
static String resultMessage = "Ready. Tests are manual; no automatic deep sleep.";

static const char *wakeName(esp_sleep_wakeup_cause_t why) {
  switch (why) {
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_EXT0: return "EXT0 / GPIO";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1 / GPIO";
    case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "cold boot / reset";
    default: return "other";
  }
}

static void begin() {
  const esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  bootMessage = String(wakeName(why));
  if (testMagic == MAGIC && lastDeepTest != 0) {
    if (lastDeepTest == DEEP_MOTION) {
      bootMessage += (why == ESP_SLEEP_WAKEUP_EXT0)
        ? " (QMI8658 INT2 motion)" : (why == ESP_SLEEP_WAKEUP_TIMER)
        ? " (WoM 60s fallback)" : " (WoM unexpected reset)";
    } else if (lastDeepTest == DEEP_TIMER) {
      bootMessage += " (15s deep-sleep test)";
    }
  }
  testMagic = 0;
  lastDeepTest = 0;
  pinMode(12, INPUT);
  pinMode(13, INPUT);
  Serial.printf("[POWER] boot wake cause: %s\n", bootMessage.c_str());
}

static uint32_t batteryMilliVolts() {
  // Schematic: BAT to GPIO8 via 200K / 100K divider (x3). Approximate only.
  return (uint32_t)analogReadMilliVolts(8) * 3UL;
}

static void detach() {
  live = nullptr;
  notice = nullptr;
  allowHardOff = false;
}

static void setNotice(const String &s) {
  resultMessage = s;
  Serial.printf("[POWER] %s\n", s.c_str());
  if (notice) lv_label_set_text(notice, resultMessage.c_str());
}
static void request(Action a) {
  if (pending != NONE || backlightUntil != 0) return;
  if (a == HARD_OFF && !allowHardOff) {
    setNotice("Tick the hard-off confirmation first.");
    return;
  }
  pending = a;
  scheduledMs = millis() + 350;
  setNotice("Starting test... watch the serial log.");
}
static void buttonCb(lv_event_t *e) {
  const auto a = static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  request(a);
}
static lv_obj_t *button(lv_obj_t *parent, const char *s, Action a) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_height(b, 39);
  lv_obj_t *t = lv_label_create(b);
  lv_label_set_text(t, s);
  lv_obj_center(t);
  lv_obj_add_event_cb(b, buttonCb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(a)));
  return b;
}
static void title(lv_obj_t *parent, const char *s) {
  lv_obj_t *t=lv_label_create(parent);
  lv_label_set_text(t,s);
  lv_obj_set_width(t,LV_PCT(100));
}
static void render(lv_obj_t *parent) {
  detach();
  lv_obj_t *column=lv_obj_create(parent);
  lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(column, 5, 0);
  lv_obj_set_style_pad_row(column, 8, 0);
  lv_obj_set_scroll_dir(column, LV_DIR_VER);
  title(column, "POWER / SLEEP LAB");
  live=lv_label_create(column);
  lv_obj_set_width(live,LV_PCT(100));
  lv_label_set_long_mode(live,LV_LABEL_LONG_WRAP);
  notice=lv_label_create(column);
  lv_obj_set_width(notice,LV_PCT(100));
  lv_label_set_long_mode(notice,LV_LABEL_LONG_WRAP);
  lv_label_set_text(notice,resultMessage.c_str());
  title(column,"Safe display / sleep checks");
  button(column,"Backlight OFF (10 sec)",BACKLIGHT_10S);
  button(column,"Light sleep (5 sec)",LIGHT_5S);
  button(column,"Deep sleep (15 sec timer)",DEEP_TIMER);
  title(column,"Motion wake: QMI8658 INT2 / GPIO12");
  lv_obj_t *options=lv_dropdown_create(column);
  lv_obj_set_width(options,LV_PCT(100));
  lv_dropdown_set_options(options,"80 mg (default)\n40 mg (sensitive)\n160 mg (strong)");
  lv_obj_add_event_cb(options,[](lv_event_t *e){
    uint16_t selected=lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    womThresholdMg = selected==1?40:(selected==2?160:80);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(column,"Deep sleep: MOVE to wake",DEEP_MOTION);
  title(column,"Motion test has a 60-second TIMER BACKUP. ESP32 reboots after wake.");
  title(column,"Power latch: GPIO7 must stay HIGH during deep sleep.");
  title(column,"Hard power off (only PWR button or USB can start again)");
  lv_obj_t *confirm=lv_checkbox_create(column);
  lv_checkbox_set_text(confirm,"Allow hardware power OFF");
  lv_obj_add_event_cb(confirm,[](lv_event_t *e){
    allowHardOff = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e),LV_STATE_CHECKED);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(column,"SYS_EN LOW: power OFF",HARD_OFF);
  title(column,"Note: screen-off is NOT ESP sleep. Board mA require an external meter.");
  lastUi=0;
}
static bool configWoM(uint8_t mg) {
  using namespace MotionWake;
  if (!ready) return false;
  // QMI8658C: CTRL7 disable all; CTRL2 +/-2g, low-power 21 Hz.
  // CAL1_L threshold in mg; CAL1_H=INT2 initially LOW + 8-sample blanking.
  // CTRL9=0x08 configures WoM. STATUS1 bit0 confirms command completed.
  if (!writeReg(0x08,0x00) || !writeReg(0x03,0x0D) ||
      !writeReg(0x0B,mg) || !writeReg(0x0C,0x48) ||
      !writeReg(0x0A,0x08)) return false;
  bool ack=false;
  uint32_t began=millis();
  while(millis()-began<250) {
    uint8_t status=0;
    if(!readRegs(0x2F,&status,1))return false;
    if(status&0x01){ack=true;break;}
    delay(3);
  }
  if(!ack)return false;
  if(!writeReg(0x08,0x01))return false;
  delay(30);
  return true;
}
static bool holdBatteryLatch() {
  digitalWrite(7,HIGH);
  // Unlike a regular GPIO output, deep sleep would leave SYS_EN floating.
  // Retain GPIO7 as output-high through deep sleep, including RTC IO.
  esp_err_t result=gpio_hold_en(GPIO_NUM_7);
  if(result!=ESP_OK) {
    setNotice(String("GPIO7 hold failed: ")+esp_err_to_name(result));
    return false;
  }
  gpio_deep_sleep_hold_en();
  return true;
}
static void unholdBatteryLatch() {
  // Keep SYS_EN high BEFORE releasing the hold.
  pinMode(7,OUTPUT);
  digitalWrite(7,HIGH);
  gpio_hold_dis(GPIO_NUM_7);
  gpio_deep_sleep_hold_dis();
}
static void enterDeep(Action a) {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if(a==DEEP_MOTION) {
    if(!configWoM(womThresholdMg)) {
      setNotice("WoM setup failed; deep sleep CANCELLED.");
      MotionWake::begin();
      return;
    }
    // Selected INT2 is initially LOW and becomes HIGH on motion.
    if(digitalRead(12)!=LOW) {
      setNotice("INT2 is HIGH before sleep; cancelled. Try again at rest.");
      MotionWake::begin();
      return;
    }
    const esp_err_t err=esp_sleep_enable_ext0_wakeup(GPIO_NUM_12,1);
    if(err!=ESP_OK) {
      setNotice(String("EXT0 failed: ")+esp_err_to_name(err));
      MotionWake::begin();
      return;
    }
  }
  const uint32_t secs=(a==DEEP_MOTION?60:15);
  const esp_err_t timerResult=esp_sleep_enable_timer_wakeup(uint64_t(secs)*1000000ULL);
  if(timerResult!=ESP_OK) {
    setNotice(String("Sleep timer failed: ")+esp_err_to_name(timerResult));
    MotionWake::begin();
    return;
  }
  if(!holdBatteryLatch()) {
    unholdBatteryLatch();
    MotionWake::begin();
    return;
  }
  testMagic=MAGIC;
  lastDeepTest=a;
  lastWomMg=womThresholdMg;
  Serial.printf("[POWER] DEEP sleep %us, mode=%u, INT2=%d\n",
                (unsigned)secs,(unsigned)a,digitalRead(12));
  Serial.flush();
  ledcWrite(5,0);
  esp_deep_sleep_start();
}

static void enterLight() {
  // ESP-IDF manually initiated light sleep cannot retain an active Wi-Fi stack.
  // The portal is restored on return; remote clients will briefly disconnect.
  Serial.println("[POWER] Light sleep, five seconds; temporarily stopping WiFi");
  Serial.flush();
  ledcWrite(5,0);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t timerErr=esp_sleep_enable_timer_wakeup(5000000ULL);
  const esp_err_t sleepErr=timerErr==ESP_OK?esp_light_sleep_start():timerErr;
  ledcWrite(5,Saver::brightness);
  WifiPortal::startAp();
  WifiPortal::startSta();
  setNotice(sleepErr==ESP_OK ? "Light sleep returned; Wi-Fi restored."
            : String("Light sleep failed: ")+esp_err_to_name(sleepErr));
}
static void tick(bool touching) {
  uint32_t now=millis();
  if(backlightUntil) {
    Saver::activity();
    if(touching || (int32_t)(now-backlightUntil)>=0) {
      backlightUntil=0;
      ledcWrite(5,Saver::brightness);
      setNotice("Backlight restored (ESP32 stayed awake).");
    }
  }
  if(live && now-lastUi>=700) {
    lastUi=now;
    lv_label_set_text_fmt(live,
      "Boot wake: %s\nBattery: ~%lu mV (GPIO8)\nIMU: %s | INT1/13=%d INT2/12=%d\nSYS_EN/7=%d | Heap: %lu KB",
      bootMessage.c_str(),(unsigned long)batteryMilliVolts(),
      MotionWake::ready?"detected":"missing",digitalRead(13),digitalRead(12),
      digitalRead(7),(unsigned long)(ESP.getFreeHeap()/1024));
  }
  if(pending==NONE || (int32_t)(now-scheduledMs)<0)return;
  const Action a=pending;
  pending=NONE;
  Saver::activity();
  if(a==BACKLIGHT_10S) {
    backlightUntil=now+10000;
    ledcWrite(5,0);
    setNotice("Backlight off: touch or wait 10 sec.");
  } else if(a==LIGHT_5S) enterLight();
  else if(a==DEEP_TIMER || a==DEEP_MOTION) enterDeep(a);
  else if(a==HARD_OFF) {
    setNotice("Power OFF; press PWR to start from battery.");
    ledcWrite(5,0);
    Serial.flush();
    digitalWrite(7,LOW);
  }
}
} // namespace PowerTests
