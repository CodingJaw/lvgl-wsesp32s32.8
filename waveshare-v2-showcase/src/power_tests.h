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
enum Action : uint8_t { NONE, BACKLIGHT_10S, LIGHT_5S, DEEP_TIMER, DEEP_MOTION, HARD_OFF, WOM_PROBE };
static RTC_DATA_ATTR uint32_t testMagic = 0;
static RTC_DATA_ATTR uint8_t lastDeepTest = 0;
static RTC_DATA_ATTR uint8_t lastWomMg = 0;
static constexpr uint32_t MAGIC = 0x50575232UL;
static Action pending = NONE;
static uint32_t scheduledMs = 0;
static uint32_t backlightUntil = 0;
static bool allowHardOff = false;
static uint8_t womThresholdMg = 80;
static bool resumePowerTab = false;
static bool probeActive = false;
static uint32_t probeStarted = 0;
static uint8_t lastWomStatus = 0;
static void restoreImu();
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
  resumePowerTab = (testMagic == MAGIC && lastDeepTest != 0);
  if (resumePowerTab) {
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
  pinMode(6, INPUT_PULLUP); // Waveshare Key_BAT / PWR SYS_OUT, active LOW
  if(resumePowerTab && why != ESP_SLEEP_WAKEUP_UNDEFINED){
    restoreImu(); // IMU could remain powered in WoM across deep-sleep reboot.
    resultMessage = "Woke from deep sleep: " + bootMessage;
  }
  Serial.printf("[POWER] boot wake cause: %s, PWR6=%d\n",
                bootMessage.c_str(),digitalRead(6));
}

static uint32_t batteryMilliVolts() {
  // Schematic: BAT to GPIO8 via 200K / 100K divider (x3). Approximate only.
  return (uint32_t)analogReadMilliVolts(8) * 3UL;
}

static void detach() {
  if(probeActive){probeActive=false;restoreImu();}
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
  button(column,"Light: touch/PWR/10s timeout",LIGHT_5S);
  button(column,"Deep sleep (15 sec timer)",DEEP_TIMER);
  title(column,"Motion wake: QMI8658 INT2 / GPIO12");
  lv_obj_t *options=lv_dropdown_create(column);
  lv_obj_set_width(options,LV_PCT(100));
  lv_dropdown_set_options(options,"80 mg (default)\n40 mg (sensitive)\n160 mg (strong)");
  lv_obj_add_event_cb(options,[](lv_event_t *e){
    uint16_t selected=lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    womThresholdMg = selected==1?40:(selected==2?160:80);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(column,"IMU motion probe (no sleep)",WOM_PROBE);
  button(column,"Deep sleep: MOVE to wake",DEEP_MOTION);
  title(column,"Try IMU probe first. Deep motion has 60s timer backup; CPU reboots.");
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
static bool womWrite(uint8_t reg,uint8_t val,const char *step) {
  if(MotionWake::writeReg(reg,val))return true;
  setNotice(String("WoM I2C failure: ")+step);
  return false;
}
static bool womCommandDone() {
  const uint32_t started=millis();
  while(millis()-started<800) {
    uint8_t status=0;
    if(!MotionWake::readRegs(0x2F,&status,1)) {
      setNotice("WoM STATUS1 I2C read failure");
      return false;
    }
    lastWomStatus=status;
    if(status & 0x01)return true;
    delay(5);
  }
  setNotice(String("WoM CTRL9 timeout: STATUS1=0x")+
    String(lastWomStatus,HEX)+" INT1="+String(digitalRead(13)));
  return false;
}
static void restoreImu() {
  // QMI8658C section 9.6: exit WoM by disabling sensors, zeroing
  // the WoM threshold and issuing CTRL9, before re-enabling accel.
  MotionWake::writeReg(0x08,0x00);
  MotionWake::writeReg(0x0B,0x00);
  MotionWake::writeReg(0x0A,0x08);
  uint8_t status=0;
  const uint32_t started=millis();
  while(millis()-started<400) {
    if(MotionWake::readRegs(0x2F,&status,1) && (status&0x01))break;
    delay(5);
  }
  MotionWake::writeReg(0x02,0x40);
  MotionWake::writeReg(0x03,0x26);
  MotionWake::writeReg(0x08,0x01);
  MotionWake::configure(Saver::motionEnabled||Saver::returnSleepEnabled,
                        Saver::motionSensitivity);
}
static bool configWoM(uint8_t mg) {
  if(!MotionWake::ready) {
    setNotice("WoM failed: QMI8658 not detected.");
    return false;
  }
  // Datasheet section 9: +/-2g at 21Hz low power;
  // CAL1_H=0x48: INT2 initially LOW, 8 samples of blanking.
  if(!womWrite(0x08,0x00,"CTRL7 disable") ||
     !womWrite(0x03,0x0D,"CTRL2 low-power") ||
     !womWrite(0x0B,mg,"CAL1_L threshold") ||
     !womWrite(0x0C,0x48,"CAL1_H INT2") ||
     !womWrite(0x0A,0x08,"CTRL9 WoM"))return false;
  if(!womCommandDone())return false;
  if(!womWrite(0x08,0x01,"CTRL7 accel enable"))return false;
  delay(60);
  uint8_t ctrl7=0;
  if(!MotionWake::readRegs(0x08,&ctrl7,1)) {
    setNotice("WoM CTRL7 verification read failed.");
    return false;
  }
  Serial.printf("[POWER] WoM: threshold=%u mg CTRL7=0x%02X INT1=%d INT2=%d\n",
    (unsigned)mg,ctrl7,digitalRead(13),digitalRead(12));
  if((ctrl7&1)==0) {
    setNotice("WoM failed: accel enable bit is LOW.");
    return false;
  }
  return true;
}
static bool clearStaleMotion() {
  if(digitalRead(12)==LOW)return true;
  uint8_t status=0;
  if(!MotionWake::readRegs(0x2F,&status,1)) {
    setNotice("INT2 HIGH and STATUS1 unreadable.");
    return false;
  }
  lastWomStatus=status;
  delay(80);
  if(digitalRead(12)==LOW)return true;
  setNotice(String("INT2 stuck HIGH: STATUS1=0x")+
            String(status,HEX)+". Could not arm wake.");
  return false;
}
static void startProbe() {
  if(!configWoM(womThresholdMg) || !clearStaleMotion()) {
    restoreImu();
    return;
  }
  // Pause normal accelerometer polling while the IMU is in WoM mode.
  MotionWake::configure(false,Saver::motionSensitivity);
  probeActive=true;
  probeStarted=millis();
  setNotice("WoM probe ARMED. Move remote now (15s timeout).");
}
static void probeTick(uint32_t now) {
  if(!probeActive)return;
  const bool irq=digitalRead(12)==HIGH;
  uint8_t status=0;
  if(!MotionWake::readRegs(0x2F,&status,1)) {
    probeActive=false;
    restoreImu();
    setNotice("WoM probe I2C read failed.");
  } else if(irq || (status & 0x04)) {
    probeActive=false;
    restoreImu();
    setNotice(String("WoM TRIGGERED! INT2=")+String(irq?1:0)+
      " STATUS1=0x"+String(status,HEX)+". IMU restored.");
  } else if(now-probeStarted>=15000) {
    probeActive=false;
    restoreImu();
    setNotice("WoM probe timed out: no motion interrupt.");
  }
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
    if(!configWoM(womThresholdMg) || !clearStaleMotion()) {
      restoreImu();
      return;
    }
    const esp_err_t err=esp_sleep_enable_ext0_wakeup(GPIO_NUM_12,1);
    if(err!=ESP_OK) {
      setNotice(String("EXT0 failed: ")+esp_err_to_name(err));
      restoreImu();
      return;
    }
  }
  const uint32_t secs=(a==DEEP_MOTION?60:15);
  const esp_err_t timerResult=esp_sleep_enable_timer_wakeup(uint64_t(secs)*1000000ULL);
  if(timerResult!=ESP_OK) {
    setNotice(String("Sleep timer failed: ")+esp_err_to_name(timerResult));
    if(a==DEEP_MOTION)restoreImu();
    return;
  }
  if(!holdBatteryLatch()) {
    unholdBatteryLatch();
    if(a==DEEP_MOTION)restoreImu();
    return;
  }
  testMagic=MAGIC;
  lastDeepTest=a;
  lastWomMg=womThresholdMg;
  setNotice(String("Entering ")+String(secs)+
     "s deep sleep; CPU will reboot on wake.");
  lv_timer_handler();
  delay(160);
  Serial.printf("[POWER] DEEP sleep %us, mode=%u, INT2=%d\n",
                (unsigned)secs,(unsigned)a,digitalRead(12));
  Serial.flush();
  ledcWrite(5,0);
  esp_deep_sleep_start();
}

static void enterLight() {
  // Active-low touch IRQ (GPIO4) and PWR SYS_OUT (GPIO6).
  // Timer provides a 10-second fallback.
  Serial.println("[POWER] Light sleep: touch / PWR / 10s timeout");
  Serial.flush();
  ledcWrite(5,0);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t timerErr=esp_sleep_enable_timer_wakeup(10000000ULL);
  bool touchArmed=false, pwrArmed=false;
  if(digitalRead(4)==HIGH &&
     gpio_wakeup_enable(GPIO_NUM_4, GPIO_INTR_LOW_LEVEL)==ESP_OK)touchArmed=true;
  if(digitalRead(6)==HIGH &&
     gpio_wakeup_enable(GPIO_NUM_6, GPIO_INTR_LOW_LEVEL)==ESP_OK)pwrArmed=true;
  if(touchArmed || pwrArmed) {
    const esp_err_t err=esp_sleep_enable_gpio_wakeup();
    if(err!=ESP_OK){
      Serial.printf("[POWER] GPIO wake configuration error: %s\n",esp_err_to_name(err));
      touchArmed=false; pwrArmed=false;
    }
  }
  Serial.printf("[POWER] Light sources: timer, touch=%d PWR=%d\n",touchArmed,pwrArmed);
  const esp_err_t sleepErr=timerErr==ESP_OK?esp_light_sleep_start():timerErr;
  const esp_sleep_wakeup_cause_t why=esp_sleep_get_wakeup_cause();
  gpio_wakeup_disable(GPIO_NUM_4);
  gpio_wakeup_disable(GPIO_NUM_6);
  ledcWrite(5,Saver::brightness);
  WifiPortal::startAp();
  WifiPortal::startSta();
  Saver::activity();
  setNotice(sleepErr==ESP_OK
    ? String("Light wake: ")+wakeName(why)+
      " | TP4="+String(digitalRead(4))+" PWR6="+String(digitalRead(6))
    : String("Light sleep failed: ")+esp_err_to_name(sleepErr));
}
static void tick(bool touching) {
  uint32_t now=millis();
  if(probeActive)probeTick(now);
  if(backlightUntil) {
    Saver::activity();
    if(touching || digitalRead(6)==LOW || (int32_t)(now-backlightUntil)>=0) {
      backlightUntil=0;
      ledcWrite(5,Saver::brightness);
      setNotice("Backlight restored (ESP32 stayed awake).");
    }
  }
  if(live && now-lastUi>=700) {
    lastUi=now;
    lv_label_set_text_fmt(live,
      "Boot wake: %s\nBattery: ~%lu mV (GPIO8)\nIMU: %s | INT1/13=%d INT2/12=%d\nPWR/6=%d TP/4=%d SYS_EN/7=%d | Heap: %lu KB",
      bootMessage.c_str(),(unsigned long)batteryMilliVolts(),
      MotionWake::ready?"detected":"missing",digitalRead(13),digitalRead(12),
      digitalRead(6),digitalRead(4),digitalRead(7),(unsigned long)(ESP.getFreeHeap()/1024));
  }
  if(pending==NONE || (int32_t)(now-scheduledMs)<0)return;
  const Action a=pending;
  pending=NONE;
  Saver::activity();
  if(a==WOM_PROBE)startProbe();
  else if(a==BACKLIGHT_10S) {
    backlightUntil=now+10000;
    ledcWrite(5,0);
    setNotice("Backlight off: touch/PWR or wait 10 sec.");
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
