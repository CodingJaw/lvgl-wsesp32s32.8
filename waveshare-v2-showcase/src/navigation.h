#pragma once
// Navigation experiments for the Waveshare ESP32-S3-Touch-LCD-2.8 V2 (240x320).
// Three switchable layouts: scrollable tabs, compact drop-down, grouped launcher.
// Existing page indexes stay unchanged (0..10, 11 optional remote).
// Hardware labs use pages 12..17. The selector skips remote if disabled.
// Does not touch IMU, sleep/wake configuration, GPIO, or scene rendering.
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include <stdint.h>
#include "custom_remote.h"

namespace Navigation {
enum Layout : uint8_t { TABS=0, SELECT=1, LAUNCHER=2 };
static Preferences prefs;
static bool initialized=false;
static Layout layout=LAUNCHER;
static lv_obj_t *root=nullptr,*navHost=nullptr,*launcher=nullptr;
static lv_obj_t *picker=nullptr,*pageTitle=nullptr;
static lv_obj_t *tabButtons[18]={};
static int currentPage=0;
static void (*goToPage)(int)=nullptr;

static bool validPage(int page) {
  return page>=0 && page<=17 && (page!=11 || CustomRemote::enabled);
}
static int pageCount() { return CustomRemote::enabled?18:17; }
static int pageAt(int visibleIndex) {
  if(visibleIndex<0 || visibleIndex>=pageCount())return -1;
  return (!CustomRemote::enabled && visibleIndex>=11)?visibleIndex+1:visibleIndex;
}
static int visibleIndex(int page) {
  if(!validPage(page))return 0;
  return (!CustomRemote::enabled && page>=12)?page-1:page;
}
static const char *pageName(int page) {
  static const char *const names[]={
    "Widgets","Controls","Chart","List",
    "Scenes","PNG","JPEG","Wi-Fi","Saver","Diag","Power",
    "Remote","RTC","microSD","Audio","I2C/UART","Gyro","BLE"
  };
  if(page==11 && CustomRemote::enabled)
    return CustomRemote::title.length()?CustomRemote::title.c_str():"Remote";
  return page>=0 && page<18?names[page]:"Unknown";
}
static void colorButton(lv_obj_t *button, bool selected) {
  if(!button)return;
  lv_obj_set_style_bg_color(button,lv_color_hex(selected?0x1D4ED8:0xE2E8F0),0);
  lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
  lv_obj_set_style_radius(button,5,0);
  lv_obj_t *text=lv_obj_get_child(button,0);
  if(text)lv_obj_set_style_text_color(text,lv_color_hex(selected?0xFFFFFF:0x1E293B),0);
}
static lv_obj_t *createButton(lv_obj_t *parent, const char *label,
                              int width,int height,lv_event_cb_t callback,
                              void *userData=nullptr) {
  lv_obj_t *button=lv_button_create(parent);
  lv_obj_set_size(button,width,height);
  lv_obj_t *text=lv_label_create(button);
  lv_label_set_text(text,label);
  lv_label_set_long_mode(text,LV_LABEL_LONG_DOT);
  lv_obj_set_width(text,LV_PCT(100));
  lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_center(text);
  colorButton(button,false);
  if(callback)lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,userData);
  return button;
}
static void closeLauncher() {
  if(!launcher)return;
  lv_obj_t *old=launcher;
  launcher=nullptr;
  // Callbacks can come from an object inside this overlay. Defer deletion
  // until LVGL finishes dispatching its event to avoid freeing the target.
  lv_obj_delete_async(old);
}
static void setCurrent(int page) {
  if(!validPage(page))return;
  currentPage=page;
  if(picker)lv_dropdown_set_selected(picker,(uint16_t)visibleIndex(page));
  if(pageTitle)lv_label_set_text(pageTitle,pageName(page));
  for(int i=0;i<18;i++)if(tabButtons[i])colorButton(tabButtons[i],i==page);
}
static void navigate(int page) {
  if(!validPage(page))return;
  closeLauncher();
  if(goToPage)goToPage(page);
}
static void pageEvent(lv_event_t *e) {
  int page=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  navigate(page);
}
static void selectorEvent(lv_event_t *e) {
  int selected=(int)lv_dropdown_get_selected(lv_event_get_target_obj(e));
  navigate(pageAt(selected));
}
static void homeEvent(lv_event_t *);
static void styleEvent(lv_event_t *);
static void stepEvent(lv_event_t *e) {
  const int offset=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int count=pageCount();
  if(count)navigate(pageAt((visibleIndex(currentPage)+offset+count)%count));
}
static void buildHeader() {
  if(!navHost)return;
  lv_obj_clean(navHost);
  picker=nullptr;
  pageTitle=nullptr;
  for(auto &button:tabButtons)button=nullptr;
  lv_obj_set_height(navHost,layout==TABS?53:45);
  lv_obj_set_flex_flow(navHost,LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(navHost,3,0);
  lv_obj_set_style_pad_column(navHost,4,0);
  lv_obj_set_scroll_dir(navHost,LV_DIR_NONE);
  lv_obj_remove_flag(navHost,LV_OBJ_FLAG_SCROLLABLE);

  createButton(navHost,"Menu",50,34,homeEvent);
  if(layout==TABS) {
    lv_obj_t *strip=lv_obj_create(navHost);
    lv_obj_set_width(strip,157);
    lv_obj_set_flex_grow(strip,1);
    lv_obj_set_height(strip,43);
    lv_obj_set_style_pad_all(strip,2,0);
    lv_obj_set_style_pad_column(strip,3,0);
    lv_obj_set_flex_flow(strip,LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(strip,LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(strip,LV_SCROLLBAR_MODE_ACTIVE);
    for(int i=0;i<pageCount();++i) {
      const int page=pageAt(i);
      tabButtons[page]=createButton(strip,pageName(page),74,33,pageEvent,
                reinterpret_cast<void *>(static_cast<intptr_t>(page)));
      colorButton(tabButtons[page],page==currentPage);
    }
  }else if(layout==SELECT) {
    picker=lv_dropdown_create(navHost);
    lv_obj_set_width(picker,150);
    lv_obj_set_flex_grow(picker,1);
    lv_obj_set_height(picker,35);
    String options;
    for(int i=0;i<pageCount();i++){
      if(i)options+="\n";
      options+=pageName(pageAt(i));
    }
    lv_dropdown_set_options(picker,options.c_str());
    lv_dropdown_set_selected(picker,(uint16_t)visibleIndex(currentPage));
    lv_obj_add_event_cb(picker,selectorEvent,LV_EVENT_VALUE_CHANGED,nullptr);
  }else {
    pageTitle=lv_label_create(navHost);
    lv_obj_set_width(pageTitle,65);
    lv_obj_set_flex_grow(pageTitle,1);
    lv_obj_set_style_text_align(pageTitle,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(pageTitle,lv_color_hex(0x1E293B),0);
    lv_label_set_long_mode(pageTitle,LV_LABEL_LONG_DOT);
    lv_label_set_text(pageTitle,pageName(currentPage));
    createButton(navHost,"<",31,34,stepEvent,
            reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    createButton(navHost,">",31,34,stepEvent,
            reinterpret_cast<void *>(static_cast<intptr_t>(1)));
  }
}
static void setLayout(Layout newLayout) {
  layout=newLayout;
  if(initialized)prefs.putUChar("layout",(uint8_t)layout);
  closeLauncher();
  buildHeader();
  Serial.printf("[NAV] layout=%u page=%d\n",(unsigned)layout,currentPage);
}
static void styleEvent(lv_event_t *e) {
  int selected=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if(selected>=0 && selected<=2)setLayout((Layout)selected);
}
static lv_obj_t *menuHeading(lv_obj_t *parent,const char *text) {
  lv_obj_t *l=lv_label_create(parent);
  lv_obj_set_width(l,LV_PCT(100));
  lv_label_set_text(l,text);
  lv_obj_set_style_text_color(l,lv_color_hex(0x334155),0);
  return l;
}
static void addCategory(lv_obj_t *parent,const char *label,
                        const int *ids,size_t count) {
  menuHeading(parent,label);
  lv_obj_t *row=lv_obj_create(parent);
  lv_obj_set_width(row,LV_PCT(100));
  lv_obj_set_height(row,LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all(row,2,0);
  lv_obj_set_style_pad_column(row,5,0);
  lv_obj_set_style_pad_row(row,5,0);
  lv_obj_set_style_border_width(row,0,0);
  lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
  lv_obj_remove_flag(row,LV_OBJ_FLAG_SCROLLABLE);
  for(size_t i=0;i<count;i++) {
    int page=ids[i];
    if(!validPage(page))continue;
    lv_obj_t *button=createButton(row,pageName(page),LV_PCT(47),41,pageEvent,
                    reinterpret_cast<void *>(static_cast<intptr_t>(page)));
    colorButton(button,page==currentPage);
  }
}
static void openLauncher() {
  if(!root||launcher)return;
  launcher=lv_obj_create(root);
  lv_obj_add_flag(launcher,LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(launcher,LV_PCT(100),LV_PCT(100));
  lv_obj_center(launcher);
  lv_obj_set_style_pad_all(launcher,3,0);
  lv_obj_set_style_pad_row(launcher,4,0);
  lv_obj_set_style_bg_color(launcher,lv_color_hex(0xF8FAFC),0);
  lv_obj_set_style_bg_opa(launcher,LV_OPA_COVER,0);
  lv_obj_set_flex_flow(launcher,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(launcher,LV_DIR_NONE);
  lv_obj_move_foreground(launcher);

  lv_obj_t *heading=lv_obj_create(launcher);
  lv_obj_set_width(heading,LV_PCT(100));
  lv_obj_set_height(heading,43);
  lv_obj_set_style_pad_all(heading,3,0);
  lv_obj_set_style_pad_column(heading,4,0);
  lv_obj_set_flex_flow(heading,LV_FLEX_FLOW_ROW);
  lv_obj_remove_flag(heading,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title=lv_label_create(heading);
  lv_obj_set_width(title,107);
  lv_obj_set_flex_grow(title,1);
  lv_label_set_text(title,"All panels");
  lv_obj_set_style_text_color(title,lv_color_hex(0x0F172A),0);
  createButton(heading,"Close",60,29,[](lv_event_t *){closeLauncher();});

  lv_obj_t *scroll=lv_obj_create(launcher);
  lv_obj_set_width(scroll,LV_PCT(100));
  lv_obj_set_flex_grow(scroll,1);
  lv_obj_set_flex_flow(scroll,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scroll,4,0);
  lv_obj_set_style_pad_row(scroll,7,0);
  lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll,LV_SCROLLBAR_MODE_ACTIVE);

  menuHeading(scroll,"Menu layout (change any time)");
  lv_obj_t *styles=lv_obj_create(scroll);
  lv_obj_set_width(styles,LV_PCT(100));
  lv_obj_set_height(styles,LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(styles,LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(styles,2,0);
  lv_obj_set_style_pad_column(styles,4,0);
  lv_obj_set_style_border_width(styles,0,0);
  lv_obj_set_style_bg_opa(styles,LV_OPA_TRANSP,0);
  lv_obj_remove_flag(styles,LV_OBJ_FLAG_SCROLLABLE);
  const char *modes[]={"Tabs","Select","Grid"};
  for(int i=0;i<3;i++){
    lv_obj_t *button=createButton(styles,modes[i],61,35,styleEvent,
                      reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    colorButton(button,layout==(Layout)i);
  }

  const int demos[]={0,1,2,3};
  const int graphics[]={4,5,6};
  const int hardware[]={9,10,12,13,14,16};
  const int connectivity[]={15,17};
  const int settings[]={7,8};
  const int remote[]={11};
  addCategory(scroll,"Interface tests",demos,4);
  addCategory(scroll,"Graphics and scenes",graphics,3);
  addCategory(scroll,"Hardware diagnostics",hardware,6);
  addCategory(scroll,"Connections and buses",connectivity,2);
  addCategory(scroll,"Wireless and settings",settings,2);
  if(CustomRemote::enabled)addCategory(scroll,"Remote",remote,1);
}
static void homeEvent(lv_event_t *) {openLauncher();}
static void refresh() {
  if(!initialized)return;
  if(!validPage(currentPage))currentPage=0;
  closeLauncher();
  buildHeader();
}
static void begin(lv_obj_t *screen,void (*navigateCallback)(int)) {
  root=screen;
  goToPage=navigateCallback;
  if(!initialized){
    prefs.begin("nav-layout",false);
    uint8_t stored=prefs.getUChar("layout",2); // Grid by default.
    layout=stored<=2?(Layout)stored:LAUNCHER;
    initialized=true;
  }
  navHost=lv_obj_create(root);
  lv_obj_set_width(navHost,LV_PCT(100));
  lv_obj_set_style_bg_color(navHost,lv_color_hex(0xF1F5F9),0);
  lv_obj_set_style_bg_opa(navHost,LV_OPA_COVER,0);
  buildHeader();
  Serial.printf("[NAV] initial layout=%u (0=tabs, 1=select, 2=grid)\n",
                (unsigned)layout);
}
} // namespace Navigation
