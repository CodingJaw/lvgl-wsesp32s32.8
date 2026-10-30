#pragma once
// ESP32-S3 BLE radio test. This source is compiled as a separate BLE-enabled
// PlatformIO environment: the normal showcase does not link/start Bluetooth,
// preserving the baseline that previously suffered a BT-controller crash.
// No scan or BLE controller starts automatically even in the BLE test build.
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#ifndef ENABLE_BLE_TEST
#define ENABLE_BLE_TEST 0
#endif
#if ENABLE_BLE_TEST
#include <BLEDevice.h>
#include <BLEScan.h>
#endif

namespace BLELab {
static bool active=false,pendingScan=false,hasScanned=false;
static lv_obj_t *status=nullptr;
static void note(const String &s) {
  Serial.printf("[BLE] %s\n",s.c_str());
  if(status)lv_label_set_text(status,s.c_str());
}
static void detach() {
  active=false;
  status=nullptr;
  pendingScan=false;
}
static void render(lv_obj_t *parent) {
  detach();
  active=true;
  lv_obj_t *pane=lv_obj_create(parent);
  lv_obj_set_size(pane,LV_PCT(100),LV_PCT(100));
  lv_obj_set_flex_flow(pane,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(pane,5,0);
  lv_obj_set_style_pad_row(pane,9,0);
  lv_obj_set_scroll_dir(pane,LV_DIR_VER);
  lv_obj_t *title=lv_label_create(pane);
  lv_label_set_text(title,"Bluetooth LE (ESP32-S3)");
  status=lv_label_create(pane);
  lv_obj_set_width(status,LV_PCT(100));
  lv_label_set_long_mode(status,LV_LABEL_LONG_WRAP);
#if ENABLE_BLE_TEST
  lv_label_set_text(status,"BLE enabled in firmware; controller NOT started.");
#else
  lv_label_set_text(status,"BLE test disabled in normal firmware. Use env waveshare28v2-ble to opt in.");
#endif
  lv_obj_t *button=lv_button_create(pane);
  lv_obj_set_size(button,LV_PCT(100),40);
  lv_obj_t *label=lv_label_create(button);
  lv_label_set_text(label,"Scan nearby BLE (3 sec)");
  lv_obj_center(label);
  lv_obj_add_event_cb(button,[](lv_event_t *){
#if ENABLE_BLE_TEST
    if(hasScanned) {
      note("A BLE scan already ran. Reboot before another test.");
      return;
    }
    pendingScan=true;
    note("Scan starting. UI may pause for about 3 seconds...");
#else
    note("This build leaves Bluetooth DISABLED. Flash waveshare28v2-ble to test.");
#endif
  },LV_EVENT_CLICKED,nullptr);
  lv_obj_t *info=lv_label_create(pane);
  lv_obj_set_width(info,LV_PCT(100));
  lv_label_set_long_mode(info,LV_LABEL_LONG_WRAP);
  lv_label_set_text(info,
    "ESP32-S3 supports BLE 5, not Bluetooth Classic.\n"
    "This opt-in test scans for nearby advertising devices without pairing.\n"
    "The separate BLE build guards the proven non-BLE firmware if controller initialization causes a crash.\n"
    "Wi-Fi/BLE coexistence may impact latency and power.");
}
static void tick(){
#if ENABLE_BLE_TEST
  if(!pendingScan || !active)return;
  pendingScan=false;
  hasScanned=true;
  Serial.println("[BLE] begin opt-in 3s scan");
  BLEDevice::init("WS28V2-Diagnostics");
  BLEScan *scanner=BLEDevice::getScan();
  scanner->setActiveScan(false);
  scanner->setInterval(160);
  scanner->setWindow(80);
  BLEScanResults found=scanner->start(3,false);
  String out="BLE advertisements: "+String(found.getCount())+"\n";
  const int n=found.getCount()<6?found.getCount():6;
  for(int i=0;i<n;i++){
    BLEAdvertisedDevice device=found.getDevice(i);
    out+=String(i+1)+": ";
    if(device.haveName())out+=String(device.getName().c_str());
    else out+="(unnamed)";
    out+=" RSSI:"+String(device.getRSSI())+" dBm\n";
  }
  scanner->clearResults();
  BLEDevice::deinit(true);
  note(out+"BLE stopped. Reboot for another scan.");
#endif
}
} // namespace BLELab
