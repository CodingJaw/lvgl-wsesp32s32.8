#pragma once
// MicroSD test on dedicated ESP32-S3 SDMMC peripheral in 1-bit mode.
// Manufacturer pin mapping: SD_CLK=GPIO14, SD_CMD=GPIO17, SD_D0=GPIO16.
// This does NOT use the LCD's SPI bus or modify its display pins.
#include <Arduino.h>
#include <lvgl.h>
#include <FS.h>
#include <SD_MMC.h>

namespace SDLab {
static bool active=false,mounted=false,allowWrite=false;
static lv_obj_t *status=nullptr,*files=nullptr,*message=nullptr;
static String lastTestPath;
static uint32_t lastUpdate=0;
static void note(const String &s) {
  Serial.printf("[SD] %s\n",s.c_str());
  if(message)lv_label_set_text(message,s.c_str());
}
static const char *typeName(uint8_t type) {
  if(type==CARD_MMC)return "MMC";
  if(type==CARD_SD)return "SD";
  if(type==CARD_SDHC)return "SDHC";
  if(type==CARD_NONE)return "No card";
  return "Unknown";
}
static bool mount() {
  if(mounted)return true;
  if(!SD_MMC.setPins(14,17,16)) {
    note("SD_MMC.setPins(14,17,16) failed");
    return false;
  }
  if(!SD_MMC.begin("/sdcard",true)) {
    note("SD mount FAILED. Check card, power, format, SD pin mapping.");
    return false;
  }
  if(SD_MMC.cardType()==CARD_NONE){
    SD_MMC.end();
    note("No SD card detected.");
    return false;
  }
  mounted=true;
  note(String("Mounted 1-bit SDMMC: ")+typeName(SD_MMC.cardType()));
  lastUpdate=0;
  return true;
}
static void unmount() {
  if(mounted)SD_MMC.end();
  mounted=false;
  allowWrite=false;
  note("SD card unmounted.");
}
static void listRoot() {
  if(!mounted){note("Mount card before listing.");return;}
  File dir=SD_MMC.open("/");
  if(!dir || !dir.isDirectory()){
    note("Could not open root directory.");
    return;
  }
  String listing;
  uint8_t count=0;
  File entry=dir.openNextFile();
  while(entry && count<12){
    listing+=(entry.isDirectory()?"[D] ":"[F] ");
    listing+=String(entry.name());
    if(!entry.isDirectory()){
      listing+=" (";
      listing+=String((unsigned long)entry.size());
      listing+=" B)";
    }
    listing+="\n";
    ++count;
    entry.close();
    entry=dir.openNextFile();
  }
  if(entry)entry.close();
  dir.close();
  if(count==0)listing="Root directory empty.";
  if(files)lv_label_set_text(files,listing.c_str());
  Serial.printf("[SD] Root list (%u entries shown):\n%s\n",count,listing.c_str());
}
static void writeTest() {
  if(!mounted){note("Mount card first.");return;}
  if(!allowWrite){note("Check 'Allow creating diagnostic file' first.");return;}
  char path[40]={};
  bool found=false;
  // Never overwrite an existing file, including earlier diagnostic runs.
  for(int i=0;i<100;i++){
    snprintf(path,sizeof(path),"/WS_D%03d.TXT",i);
    if(!SD_MMC.exists(path)){found=true;break;}
  }
  if(!found){note("Diagnostic file slots exhausted; write cancelled.");return;}
  File out=SD_MMC.open(path,FILE_WRITE);
  if(!out){note("Failed to create test file.");return;}
  const String text="Waveshare S3 Touch LCD 2.8 V2 SDMMC test\n";
  const size_t bytes=out.print(text);
  out.flush();
  out.close();
  if(bytes!=text.length()) {
    note("Write incomplete; inspect card filesystem.");
    return;
  }
  lastTestPath=String(path);
  note("Created "+lastTestPath+" ("+String(bytes)+" bytes). Read it next.");
  allowWrite=false;
  listRoot();
}
static void readTest() {
  if(!mounted){note("Mount card first.");return;}
  if(lastTestPath.isEmpty()){
    note("No test file from this session; write a new one first.");
    return;
  }
  File input=SD_MMC.open(lastTestPath.c_str(),FILE_READ);
  if(!input){note("Could not open "+lastTestPath);return;}
  char buff[96]={};
  const int n=input.readBytes(buff,sizeof(buff)-1);
  input.close();
  note("Read "+lastTestPath+" ("+String(n)+" bytes): "+String(buff));
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
  active=false;
  status=files=message=nullptr;
  // Leave the card mounted for possible later image/profile file support.
  // Manual "Unmount" is provided for removal/power testing.
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
  lv_obj_t *heading=lv_label_create(pane);
  lv_label_set_text(heading,"microSD / TF card");
  status=lv_label_create(pane);
  lv_obj_set_width(status,LV_PCT(100));
  lv_label_set_long_mode(status,LV_LABEL_LONG_WRAP);
  files=lv_label_create(pane);
  lv_obj_set_width(files,LV_PCT(100));
  lv_label_set_long_mode(files,LV_LABEL_LONG_WRAP);
  lv_label_set_text(files,"Root files will appear after List.");
  message=lv_label_create(pane);
  lv_obj_set_width(message,LV_PCT(100));
  lv_label_set_long_mode(message,LV_LABEL_LONG_WRAP);
  lv_label_set_text(message,"Nothing is mounted or written automatically.");
  button(pane,"Mount card (1-bit SDMMC)",[](lv_event_t *){mount();});
  button(pane,"List root files",[](lv_event_t *){listRoot();});
  lv_obj_t *confirm=lv_checkbox_create(pane);
  lv_checkbox_set_text(confirm,"Allow creating diagnostic file");
  lv_obj_add_event_cb(confirm,[](lv_event_t *e){
    allowWrite=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
  },LV_EVENT_VALUE_CHANGED,nullptr);
  button(pane,"Write NEW test file",[](lv_event_t *){writeTest();});
  button(pane,"Read last test file",[](lv_event_t *){readTest();});
  button(pane,"Unmount safely",[](lv_event_t *){unmount();});
  lv_obj_t *hint=lv_label_create(pane);
  lv_obj_set_width(hint,LV_PCT(100));
  lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint,
    "SDMMC CLK14 CMD17 D0=16. Does not share LCD SPI.\n"
    "Unmount before removing card.\n"
    "Write test creates a unique TXT file; it never overwrites existing data.\n"
    "No formatting or deletion is performed.");
  lastUpdate=0;
}
static void tick(uint32_t now) {
  if(!active)return;
  if(lastUpdate && now-lastUpdate<1000)return;
  lastUpdate=now;
  if(!status)return;
  if(!mounted){
    lv_label_set_text(status,"Not mounted (click Mount)");
    return;
  }
  lv_label_set_text_fmt(status,
    "Card: %s\nCapacity: %llu MB\nUsed: %llu MB / %llu MB",
    typeName(SD_MMC.cardType()),
    (unsigned long long)(SD_MMC.cardSize()/1048576ULL),
    (unsigned long long)(SD_MMC.usedBytes()/1048576ULL),
    (unsigned long long)(SD_MMC.totalBytes()/1048576ULL));
}
} // namespace SDLab
