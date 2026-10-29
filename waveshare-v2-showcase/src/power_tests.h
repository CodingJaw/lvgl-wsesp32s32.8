#pragma once
// Power lab for Waveshare ESP32-S3-Touch-LCD-2.8 V2.
// Schematic: QMI8658 INT1=GPIO13, INT2=GPIO12, BAT_ADC=GPIO8,
// SYS_EN/BAT_Control=GPIO7, backlight=GPIO5.
// WoM sequence follows QST datasheet Rev 0.6/0.9 and Waveshare newer driver.
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
  if(probeActive) {
    setNotice("WoM probe running. Move remote or allow its 15s timeout.");
    return;
  }
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
  button(column,"Probe IMU: SCREEN STAYS ON",WOM_PROBE);
  button(column,"Deep sleep: MOVE to wake",DEEP_MOTION);
  title(column,"Probe checks IMU interrupt while AWAKE. Deep motion blanks the display ONLY if setup succeeds.");
  title(column,"Deep motion has 60s timer fallback; the CPU restarts on wake.");
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
// QMI8658C command protocol differs between older and newer firmware.
// Waveshare's newer QMI8658C driver checks STATUSINT(0x2D).bit7 and
// writes CTRL9=0 to acknowledge; earlier QST Rev 0.6 uses STATUS1(0x2F).bit0.
// Do NOT read STATUS1 repeatedly while waiting for motion: it CLEARS the WoM
// event and releases INT2 (datasheet section 9.3).
static uint8_t lastStatusInt=0, lastStatus1=0, sensorRevision=0;
static uint8_t ctrl1AtWom=0;
static const char *commandProtocol="not run";
static uint32_t probeLastReport=0;

static bool womWrite(uint8_t reg,uint8_t val,const char *step) {
  if(MotionWake::writeReg(reg,val))return true;
  setNotice(String("WoM I2C WRITE FAIL: ")+step+
            " reg=0x"+String(reg,HEX));
  return false;
}
static bool commandWoM(bool reportError) {
  if(!MotionWake::writeReg(0x0A,0x08)) {
    if(reportError)setNotice("WoM CTRL9 0x08 write FAILED");
    return false;
  }
  const uint32_t started=millis();
  bool modern=false,legacy=false;
  // Prefer modern QMI8658C firmware status and acknowledge sequence.
  // Poll legacy CMD_DONE as well, so older chips are supported.
  while(millis()-started<650) {
    uint8_t v=0;
    if(!MotionWake::readRegs(0x2D,&v,1)){
      if(reportError)setNotice("WoM STATUSINT 0x2D I2C read FAILED");
      return false;
    }
    lastStatusInt=v;
    if(v&0x80){modern=true;break;}
    if(!MotionWake::readRegs(0x2F,&v,1)){
      if(reportError)setNotice("WoM STATUS1 0x2F I2C read FAILED");
      return false;
    }
    lastStatus1=v;
    if(v&0x01){legacy=true;break;}
    delay(4);
  }
  if(!modern && !legacy){
    commandProtocol="no completion";
    if(reportError)setNotice(String("WoM CTRL9 timeout: 2D=0x")+
      String(lastStatusInt,HEX)+" 2F=0x"+String(lastStatus1,HEX));
    return false;
  }
  commandProtocol=modern?"modern 2D.bit7":"legacy 2F.bit0";
  // New protocol requires the explicit 0x00 ACK, unlike the old protocol.
  if(modern){
    if(!MotionWake::writeReg(0x0A,0x00)){
      if(reportError)setNotice("WoM CTRL9 ACK 0x00 FAILED");
      return false;
    }
    const uint32_t ackStart=millis();
    bool cleared=false;
    while(millis()-ackStart<350){
      uint8_t v=0;
      if(!MotionWake::readRegs(0x2D,&v,1)){
        if(reportError)setNotice("WoM ACK verify 0x2D read FAILED");
        return false;
      }
      lastStatusInt=v;
      if((v&0x80)==0){cleared=true;break;}
      delay(4);
    }
    if(!cleared){
      if(reportError)setNotice(String("WoM ACK stayed HIGH: 2D=0x")+
          String(lastStatusInt,HEX));
      return false;
    }
  }
  Serial.printf("[POWER] WoM CMD_DONE via %s (2D=%02X 2F=%02X, ms=%lu)\n",
    commandProtocol,lastStatusInt,lastStatus1,(unsigned long)(millis()-started));
  return true;
}
static void restoreImu() {
  if(!MotionWake::ready)return;
  // QMI datasheet section 9.6: stop accelerometer and send WoM threshold
  // of zero to exit the mode. Restore the already-tested polling settings.
  MotionWake::writeReg(0x08,0x00);
  MotionWake::writeReg(0x0B,0x00);
  (void)commandWoM(false);
  MotionWake::writeReg(0x02,0x40); // original tested CTRL1; INT2 output disabled
  MotionWake::writeReg(0x03,0x26); // +/-8g, 125Hz
  MotionWake::writeReg(0x08,0x01); // accel on
  MotionWake::configure(Saver::motionEnabled||Saver::returnSleepEnabled,
                        Saver::motionSensitivity);
  Serial.println("[POWER] QMI8658 normal polling mode restored");
}
static bool configWoM(uint8_t mg) {
  if(!MotionWake::ready) {
    setNotice("WoM blocked: QMI8658 not detected.");
    return false;
  }
  if(!MotionWake::readRegs(0x01,&sensorRevision,1)){
    setNotice("WoM: cannot read chip revision 0x01.");
    return false;
  }
  // INT2 enable (CTRL1 bit4) is required by Waveshare's *newer* driver.
  // Preserve SPI_AI (bit6), which the existing motion driver uses.
  uint8_t ctrl1=0;
  if(!MotionWake::readRegs(0x02,&ctrl1,1)){
    setNotice("WoM: cannot read CTRL1 0x02.");
    return false;
  }
  if(!womWrite(0x08,0x00,"CTRL7 stop sensors") ||
     !womWrite(0x02,ctrl1 | 0x10,"CTRL1 INT2 enable") ||
     !womWrite(0x03,0x0D,"CTRL2 +/-2g 21Hz low-power") ||
     !womWrite(0x0B,mg,"CAL1_L threshold mg") ||
     !womWrite(0x0C,0x48,"CAL1_H INT2 idle LOW 8 samples"))
    return false;
  if(!commandWoM(true))return false;
  if(!womWrite(0x08,0x01,"CTRL7 accelerometer ON"))return false;
  delay(90);
  uint8_t ctrl7=0;
  if(!MotionWake::readRegs(0x08,&ctrl7,1) ||
     !MotionWake::readRegs(0x02,&ctrl1AtWom,1)) {
    setNotice("WoM: CTRL7/CTRL1 verification I2C failed.");
    return false;
  }
  if((ctrl7&0x01)==0){
    setNotice(String("WoM: accel enable did not stick. CTRL7=0x")+
              String(ctrl7,HEX));
    return false;
  }
  Serial.printf("[POWER] WoM READY: chip rev=0x%02X, CTRL1=0x%02X, CTRL7=0x%02X, threshold=%u mg, INT1=%d INT2=%d\n",
    sensorRevision,ctrl1AtWom,ctrl7,(unsigned)mg,digitalRead(13),digitalRead(12));
  return true;
}
static bool clearStaleMotion() {
  // The selected INT2 is low before the first WoM event. An existing HIGH
  // signal must be acknowledged or ESP32 EXT0 would wake instantly.
  if(digitalRead(12)==LOW)return true;
  uint8_t status=0;
  if(!MotionWake::readRegs(0x2F,&status,1)){
    setNotice("WoM: INT2 HIGH and STATUS1 unreadable.");
    return false;
  }
  lastStatus1=status;
  delay(60);
  if(digitalRead(12)==LOW){
    Serial.printf("[POWER] Cleared stale WoM event STATUS1=0x%02X\n",status);
    return true;
  }
  setNotice(String("WoM: INT2 stays HIGH; status=0x")+
            String(status,HEX)+" CTRL1=0x"+String(ctrl1AtWom,HEX));
  return false;
}
static void startProbe() {
  // This is intentionally an AWAKE hardware IRQ test. The LCD stays ON.
  setNotice("Probe: configuring IMU; display stays ON.");
  lv_timer_handler();
  if(!configWoM(womThresholdMg) || !clearStaleMotion()){
    restoreImu();
    return;
  }
  MotionWake::configure(false,Saver::motionSensitivity);
  probeActive=true;
  probeStarted=millis();
  probeLastReport=0;
  setNotice(String("WoM ARMED (screen stays ON). Move within 15s. Protocol: ")+
            commandProtocol);
}
static void probeTick(uint32_t now) {
  if(!probeActive)return;
  const bool irq=digitalRead(12)==HIGH;
  // Check GPIO FIRST. Reading STATUS1 clears the event/IRQ on the chip.
  if(irq){
    uint8_t status=0;
    bool ok=MotionWake::readRegs(0x2F,&status,1);
    probeActive=false;
    restoreImu();
    if(ok)setNotice(String("SUCCESS: INT2 rose HIGH; WoM status=0x")+
                    String(status,HEX)+" (bit2="+String((status&0x04)?1:0)+")");
    else setNotice("INT2 went HIGH, but STATUS1 read failed.");
    return;
  }
  if(now-probeStarted>=15000){
    uint8_t status=0;
    const bool ok=MotionWake::readRegs(0x2F,&status,1);
    probeActive=false;
    restoreImu();
    setNotice(String("TIMEOUT: INT2 did not rise; 2F=")+
              (ok?String(status,HEX):String("I2C fail"))+
              " CTRL1=0x"+String(ctrl1AtWom,HEX)+
              " CMD="+commandProtocol);
    return;
  }
  if(now-probeLastReport>=1000){
    probeLastReport=now;
    if(notice)lv_label_set_text_fmt(notice,
      "WoM ARMED; screen stays ON.\nMove now. %lus left\nINT2=%d rev=%02X CTRL1=%02X\nCMD: %s",
      (unsigned long)(15-(now-probeStarted)/1000),digitalRead(12),
      sensorRevision,ctrl1AtWom,commandProtocol);
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
    setNotice("Configuring IMU motion wake...");
    lv_timer_handler();
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
