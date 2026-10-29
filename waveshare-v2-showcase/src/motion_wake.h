#pragma once
#include <Wire.h>
// QMI8658 at 0x6B, separate I2C bus from the CST3530 touch panel.
// Accelerometer vector delta detects lift, tilt and motion without needing
// to integrate gyro drift. Polling is appropriate while the ESP stays awake.
namespace MotionWake {
static TwoWire imuBus(0);
static constexpr uint8_t ADDR=0x6B;
static bool ready=false,hasSample=false;
static int16_t ax=0,ay=0,az=0;
static uint32_t lastPoll=0,lastTrigger=0;
static uint8_t sensitivity=1; // 0 gentle, 1 normal, 2 strong movement
static bool enabled=false;
static bool fresh=false;
static int16_t previousX=0,previousY=0,previousZ=0;
static int32_t deltaMagnitude=0;
static bool writeReg(uint8_t reg,uint8_t value) {
  imuBus.beginTransmission(ADDR);
  imuBus.write(reg);imuBus.write(value);
  return imuBus.endTransmission()==0;
}
static bool readRegs(uint8_t reg,uint8_t *buf,size_t n) {
  imuBus.beginTransmission(ADDR);imuBus.write(reg);
  if(imuBus.endTransmission(false)!=0)return false;
  if(imuBus.requestFrom(ADDR,n)!=(int)n)return false;
  for(size_t i=0;i<n;++i)buf[i]=imuBus.read();
  return true;
}
static void begin() {
  imuBus.begin(11,10,100000);
  uint8_t id=0;
  ready=readRegs(0x00,&id,1)&&id==0x05;
  if(ready){
    // Auto-increment, 8g accel range at 125 Hz; accel only to save power.
    ready=writeReg(0x02,0x40)&&writeReg(0x03,0x26)&&writeReg(0x08,0x01);
  }
  Serial.printf("[IMU] QMI8658: %s (id=0x%02x)\n",ready?"ready":"not detected",id);
}
static bool poll() {
  fresh=false;
  if(!ready || !enabled)return false;
  uint32_t now=millis();
  if(now-lastPoll<60)return false;
  lastPoll=now;
  uint8_t data[6];
  if(!readRegs(0x35,data,sizeof(data))){hasSample=false;return false;}
  int16_t x=(int16_t)(uint16_t(data[0])|(uint16_t(data[1])<<8));
  int16_t y=(int16_t)(uint16_t(data[2])|(uint16_t(data[3])<<8));
  int16_t z=(int16_t)(uint16_t(data[4])|(uint16_t(data[5])<<8));
  if(!hasSample){ax=x;ay=y;az=z;hasSample=true;return false;}
  // Difference from previous sample, not absolute orientation: a stationary
  // remote will not trigger, even when resting at an angle.
  int32_t dx=int32_t(x)-ax,dy=int32_t(y)-ay,dz=int32_t(z)-az;
  ax=x;ay=y;az=z;
  fresh=true;
  deltaMagnitude=(int32_t)sqrtf(float(int64_t(dx)*dx+int64_t(dy)*dy+int64_t(dz)*dz));
  const int32_t thresholds[]={360,750,1400}; // ~0.09g, 0.18g, 0.34g @ +/-8g
  int32_t threshold=thresholds[sensitivity<3?sensitivity:1];
  bool moved=(int64_t(dx)*dx+int64_t(dy)*dy+int64_t(dz)*dz)>int64_t(threshold)*threshold;
  if(moved && now-lastTrigger>900){lastTrigger=now;return true;}
  return false;
}
static void configure(bool on,uint8_t level){
  enabled=on;sensitivity=level<3?level:1;
  hasSample=false;
  fresh=false;
}
}
