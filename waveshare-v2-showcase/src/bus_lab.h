#pragma once
// Pin-safe expansion tests. I2C scans existing MotionWake::imuBus without
// changing its pins; UART loopback is started ONLY after an explicit click.
// GPIO15/18 are sampled as inputs, never driven.
#include <Arduino.h>
#include <lvgl.h>
#include "motion_wake.h"

namespace BusLab {
static bool active=false,uartActive=false,uartConfirmed=false;
static uint32_t lastRefresh=0,uartStart=0;
static String uartReceived;
static lv_obj_t *live=nullptr,*scanResult=nullptr,*message=nullptr;
static void note(const String &s){
  Serial.printf("[EXP] %s\n",s.c_str());
  if(message)lv_label_set_text(message,s.c_str());
}
static void scan() {
  if(!MotionWake::ready) {
    note("IMU bus may be unavailable; scanning anyway on GPIO10/11");
  }
  String names;
  uint8_t found=0,errors=0;
  for(uint8_t address=0x08;address<=0x77;address++){
    MotionWake::imuBus.beginTransmission(address);
    const uint8_t response=MotionWake::imuBus.endTransmission();
    if(response==0){
      if(found)names+=", ";
      names+="0x"+String(address,HEX);
      if(address==0x51)names+="(RTC)";
      if(address==0x6B)names+="(QMI)";
      if(address==0x70)names+="(TCA9548A?)";
      if(address==0x34)names+="(TCA8418?)";
      found++;
    }else if(response!=2)errors++;
  }
  String out="I2C SDA11 SCL10\n"+String(found)+" device(s):\n"+
    (found?names:String("none"))+"\nBus errors:"+String(errors);
  if(scanResult)lv_label_set_text(scanResult,out.c_str());
  note("Scan complete. "+String(found)+" ACK(s), "+String(errors)+" errors.");
}
static void beginUart() {
  if(!uartConfirmed){
    note("Connect GPIO43 TX to GPIO44 RX (3.3V) and check confirmation first.");
    return;
  }
  if(uartActive){
    Serial1.end();
    uartActive=false;
  }
  Serial1.begin(115200,SERIAL_8N1,44,43);
  delay(10);
  while(Serial1.available())Serial1.read();
  uartReceived="";
  uartStart=millis();
  const char payload[]="WS28V2_UART_LOOP";
  Serial1.write(reinterpret_cast<const uint8_t *>(payload),sizeof(payload)-1);
  Serial1.flush();
  uartActive=true;
  note("UART test bytes sent on GPIO43; awaiting GPIO44 echo...");
}
static void detach(){
  if(uartActive)Serial1.end();
  uartActive=false;
  uartConfirmed=false;
  active=false;
  live=scanResult=message=nullptr;
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,lv_event_cb_t cb) {
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_size(b,LV_PCT(100),38);
  lv_obj_t *l=lv_label_create(b);
  lv_label_set_text(l,name);
  lv_obj_center(l);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
  return b;
}
static void render(lv_obj_t *parent){
  detach();
  active=true;
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_style_pad_all(pane,4,0);
  lv_obj_set_style_pad_row(pane,8,0);
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(pane,LV_DIR_VER);
  lv_obj_t *heading=lv_label_create(pane);
  lv_label_set_text(heading,"Expansion I2C / UART / GPIO");
  live=lv_label_create(pane);
  lv_obj_set_width(live,LV_PCT(100));
  lv_label_set_text(live,"GPIO inputs...");
  scanResult=lv_label_create(pane);
  lv_obj_set_width(scanResult,LV_PCT(100));
  lv_label_set_long_mode(scanResult,LV_LABEL_LONG_WRAP);
  lv_label_set_text(scanResult,"Tap Scan for onboard and external I2C device addresses.");
  message=lv_label_create(pane);
  lv_obj_set_width(message,LV_PCT(100));
  lv_label_set_long_mode(message,LV_LABEL_LONG_WRAP);
  lv_label_set_text(message,"No GPIO is driven automatically.");
  button(pane,"Scan I2C (GPIO10/11)",[](lv_event_t *){scan();});
  lv_obj_t *confirm=lv_checkbox_create(pane);
  lv_checkbox_set_text(confirm,"GPIO43 TX <-> GPIO44 RX jumper attached");
  lv_obj_add_event_cb(confirm,[](lv_event_t *e){
    uartConfirmed=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(pane,"Test UART loopback",[](lv_event_t *){beginUart();});
  lv_obj_t *hint=lv_label_create(pane);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "12-pin header: 3V3, GND, SDA11, SCL10, TX43, RX44, GPIO15, GPIO18, USB D+/D-.\n"
    "Known I2C: RTC=0x51, QMI8658=0x6B. Test external TCA8418/TCA9548A once wired.\n"
    "GPIO15 and GPIO18 are READ ONLY here; unconnected pins may float.\n"
    "Never connect a 5V output directly to 3.3V ESP32 inputs.");
  lastRefresh=0;
}
static void tick(uint32_t now){
  if(!active)return;
  if(uartActive){
    while(Serial1.available() && uartReceived.length()<80){
      uartReceived+=char(Serial1.read());
    }
    if(uartReceived.indexOf("WS28V2_UART_LOOP")>=0){
      uartActive=false;
      Serial1.end();
      note("UART LOOPBACK PASS: exact 17-byte test echo received.");
    }else if((uint32_t)(now-uartStart)>=1500){
      uartActive=false;
      Serial1.end();
      note("UART TIMEOUT. Received: "+uartReceived+" (check TX43->RX44).");
    }
  }
  if(lastRefresh && now-lastRefresh<500)return;
  lastRefresh=now;
  if(live)lv_label_set_text_fmt(live,
    "GPIO15:%d GPIO18:%d\nRTC IRQ9:%d IMU INT1/13:%d INT2/12:%d\nPWR6:%d",
    digitalRead(15),digitalRead(18),digitalRead(9),
    digitalRead(13),digitalRead(12),digitalRead(6));
}
} // namespace BusLab
