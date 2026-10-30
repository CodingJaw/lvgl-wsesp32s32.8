#pragma once
// PCM5101A audio DAC -> on-board amplifier and speaker.
// ESP32-S3 I2S: BCK=GPIO48, LRCK/WS=GPIO38, DOUT=GPIO47.
// Generates original sine tones; no external audio files or libraries.
// Audio is completely off until the user presses Play.
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <ESP_I2S.h>

namespace AudioLab {
static constexpr uint32_t RATE=22050;
static I2SClass audio;
static lv_obj_t *status=nullptr,*level=nullptr;
static bool active=false,installed=false,playing=false,draining=false;
static uint32_t drainUntil=0;
static uint32_t frame=0,targetFrames=0;
static uint16_t frequency=440;
static int gainPercent=20;
static uint32_t elapsedWriteUs=0;
static String lastError;
static void note(const String &s) {
  lastError=s;
  Serial.printf("[AUDIO] %s\n",s.c_str());
  if(status)lv_label_set_text(status,s.c_str());
}
static void stop() {
  playing=false;
  draining=false;
  if(installed)audio.end();
  installed=false;
}
static bool initialize() {
  if(installed)return true;
  // Arduino-ESP32 3.3.x ESP_I2S driver (not legacy IDF 4.x i2s.h).
  audio.setPins(48,38,47); // BCLK, WS/LRCK, DOUT -> PCM5101
  if(!audio.begin(I2S_MODE_STD,RATE,I2S_DATA_BIT_WIDTH_16BIT,
                  I2S_SLOT_MODE_STEREO)) {
    note(String("I2S begin failed: ")+String(audio.lastError()));
    return false;
  }
  installed=true;
  return true;
}
static void startTone(uint16_t hz) {
  stop();
  if(!initialize())return;
  frequency=hz;
  frame=0;
  targetFrames=RATE*2/5; // 400ms
  elapsedWriteUs=0;
  playing=true;
  note("Playing "+String(frequency)+" Hz for 400ms at "+String(gainPercent)+"% amplitude");
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
static void detach() {
  stop();
  active=false;
  status=level=nullptr;
}
static void render(lv_obj_t *parent) {
  detach();
  active=true;
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_style_pad_all(pane,4,0);
  lv_obj_set_style_pad_row(pane,9,0);
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(pane,LV_DIR_VER);
  lv_obj_t *title=lv_label_create(pane);
  lv_label_set_text(title,"Audio - PCM5101A I2S");
  status=lv_label_create(pane);
  lv_obj_set_width(status,LV_PCT(100));
  lv_label_set_long_mode(status,LV_LABEL_LONG_WRAP);
  lv_label_set_text(status,"Audio idle; I2S driver not installed.");
  level=lv_label_create(pane);
  lv_label_set_text(level,"Output amplitude: 20%");
  lv_obj_t *vol=lv_slider_create(pane);
  lv_obj_set_width(vol,LV_PCT(100));
  lv_slider_set_range(vol,5,40); // cap software amplitude to avoid loud tones
  lv_slider_set_value(vol,gainPercent,LV_ANIM_OFF);
  lv_obj_add_event_cb(vol,[](lv_event_t *e){
    gainPercent=lv_slider_get_value(lv_event_get_target_obj(e));
    if(level)lv_label_set_text_fmt(level,"Output amplitude: %d%%",gainPercent);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(pane,"Play 440 Hz tone",[](lv_event_t *){startTone(440);});
  button(pane,"Play 880 Hz tone",[](lv_event_t *){startTone(880);});
  button(pane,"Stop sound",[](lv_event_t *){stop();note("Stopped, I2S released.");});
  lv_obj_t *hint=lv_label_create(pane);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "Pins: BCK48, LRCK38, DATA47.\n"
    "Connect 8 ohm speaker and turn the board's hardware volume knob low first.\n"
    "Audio may depend on the board's amplifier supply; try USB power if silent.\n"
    "No automatic audio at startup. The I2S driver is released after playback.");
}
static void tick() {
  if(!active)return;
  if(draining) {
    if((int32_t)(millis()-drainUntil)>=0) {
      draining=false;
      stop();
      note(String("Tone finished. I2S released; queued audio drained."));
    }
    return;
  }
  if(!playing)return;
  int16_t samples[2*256];
  uint32_t count=targetFrames-frame;
  if(count>256)count=256;
  const float amplitude=32767.0f*float(gainPercent)/100.0f;
  for(uint32_t i=0;i<count;i++){
    const float phase=6.28318530718f*float((frame+i)%RATE)*float(frequency)/float(RATE);
    float envelope=1.0f;
    const uint32_t pos=frame+i;
    if(pos<220)envelope=float(pos)/220.0f;
    if(targetFrames-pos<220)envelope=float(targetFrames-pos)/220.0f;
    const int16_t pcm=int16_t(sinf(phase)*amplitude*envelope);
    samples[i*2]=pcm;
    samples[i*2+1]=pcm;
  }
  const uint32_t before=micros();
  const size_t written=audio.write((const uint8_t *)samples,count*4);
  elapsedWriteUs+=micros()-before;
  if(written!=count*4){
    note(String("I2S write incomplete: bytes=")+String((unsigned)written)+
         " expected="+String(unsigned(count*4)));
    stop();
    return;
  }
  frame+=count;
  if(frame>=targetFrames){
    // Allow queued DMA buffers to drain without blocking the LVGL loop.
    playing=false;
    draining=true;
    drainUntil=millis()+150;
    note(String("PCM queued; draining I2S. Write time: ")+
         String(elapsedWriteUs/1000)+"ms");
  }
}
} // namespace AudioLab
