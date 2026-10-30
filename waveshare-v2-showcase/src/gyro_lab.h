#pragma once
// Optional QMI8658C gyro exercise. Normal showcase only enables the accel.
// Enable the gyro only when requested; ALWAYS restore saved CTRL3 and
// accel-only CTRL7 on tab exit so the proven Wake-on-Motion test is preserved.
#include <Arduino.h>
#include <lvgl.h>
#include "motion_wake.h"

namespace GyroLab {
static bool active=false,running=false;
static uint8_t savedCtrl3=0;
static uint32_t lastRead=0;
static lv_obj_t *data=nullptr,*noteText=nullptr;
static int32_t peakDps=0;
static void note(const String &s){
  Serial.printf("[GYRO] %s\n",s.c_str());
  if(noteText)lv_label_set_text(noteText,s.c_str());
}
static bool start(){
  if(!MotionWake::ready){note("QMI8658 unavailable.");return false;}
  if(running)return true;
  if(!MotionWake::readRegs(0x04,&savedCtrl3,1)){
    note("Cannot read previous CTRL3 setting");
    return false;
  }
  // CTRL3 0x56 = +/-512 deg/s, 125Hz ODR.
  // CTRL7 0x03 enables accel + gyro, same I2C device.
  if(!MotionWake::writeReg(0x08,0x00) ||
     !MotionWake::writeReg(0x04,0x56) ||
     !MotionWake::writeReg(0x08,0x03)){
    MotionWake::writeReg(0x08,0x01);
    MotionWake::writeReg(0x04,savedCtrl3);
    note("Gyroscope register configuration failed.");
    return false;
  }
  peakDps=0;
  running=true;
  lastRead=0;
  note("Gyroscope ON (512 dps range). Move or rotate the board.");
  return true;
}
static void stop(){
  if(running){
    MotionWake::writeReg(0x08,0x01); // return to accel ONLY first
    MotionWake::writeReg(0x04,savedCtrl3);
    running=false;
    Serial.println("[GYRO] gyro OFF, accel-only mode restored");
  }
}
static void detach() {
  stop();
  active=false;
  data=noteText=nullptr;
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,lv_event_cb_t cb){
  lv_obj_t *b=lv_button_create(parent);
  lv_obj_set_size(b,LV_PCT(100),39);
  lv_obj_t *label=lv_label_create(b);
  lv_label_set_text(label,name);
  lv_obj_center(label);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
  return b;
}
static void render(lv_obj_t *parent) {
  detach();
  active=true;
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_style_pad_all(pane,4,0);
  lv_obj_set_style_pad_row(pane,8,0);
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(pane,LV_DIR_VER);
  lv_obj_t *title=lv_label_create(pane);
  lv_label_set_text(title,"QMI8658 3-axis gyroscope");
  data=lv_label_create(pane);
  lv_obj_set_width(data,LV_PCT(100));
  lv_label_set_text(data,"Gyroscope OFF / idle.");
  noteText=lv_label_create(pane);
  lv_obj_set_width(noteText,LV_PCT(100));
  lv_label_set_long_mode(noteText,LV_LABEL_LONG_WRAP);
  lv_label_set_text(noteText,"Normal mode uses the accelerometer only.");
  button(pane,"Enable gyro (test)",[](lv_event_t *){start();});
  button(pane,"Disable gyro",[](lv_event_t *){stop();note("Gyroscope disabled, accel restored.");});
  lv_obj_t *hint=lv_label_create(pane);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "Samples angular velocity (deg/s). X, Y, Z directions depend on board orientation.\n"
    "This consumes additional power only while enabled.\n"
    "Leaving this page automatically disables the gyro and restores accel-only operation.\n"
    "Wake-on-Motion uses the accelerometer, not the gyroscope.");
}
static void tick(uint32_t now){
  if(!active || !data)return;
  if(!running){
    lv_label_set_text(data,"Gyroscope OFF. Press Enable to sample.");
    return;
  }
  if(lastRead && now-lastRead<200)return;
  lastRead=now;
  uint8_t buf[6]={};
  if(!MotionWake::readRegs(0x3B,buf,6)){
    note("Gyro read failed (0x3B).");
    return;
  }
  const int16_t x=int16_t(uint16_t(buf[0])|(uint16_t(buf[1])<<8));
  const int16_t y=int16_t(uint16_t(buf[2])|(uint16_t(buf[3])<<8));
  const int16_t z=int16_t(uint16_t(buf[4])|(uint16_t(buf[5])<<8));
  // At +/-512dps: 64 raw LSB per degree/sec.
  const int32_t mag=int32_t(sqrtf(float(int64_t(x)*x+int64_t(y)*y+int64_t(z)*z))/64.0f);
  if(mag>peakDps)peakDps=mag;
  lv_label_set_text_fmt(data,
    "X: %ld dps\nY: %ld dps\nZ: %ld dps\n"
    "Rotation magnitude: %ld dps\nPeak: %ld dps\nINT1:%d INT2:%d",
    long(x)/64,long(y)/64,long(z)/64,
    long(mag),long(peakDps),digitalRead(13),digitalRead(12));
}
} // namespace GyroLab
