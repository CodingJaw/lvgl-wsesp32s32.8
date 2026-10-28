#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>

// Confirmed from the Waveshare V2 swipe-test serial output.
static constexpr int LCD_SCK = 40;
static constexpr int LCD_MOSI = 45;
static constexpr int LCD_CS = 42;
static constexpr int LCD_DC = 41;
static constexpr int LCD_RST = 39;
static constexpr int WIDTH = 240;
static constexpr int HEIGHT = 320;

// Touch pins/protocol MUST be imported from the verified CST3530 swipe-test
// firmware before enabling touch. Do not assume CST328 and CST3530 match.
static constexpr bool TOUCH_READY = true;
static constexpr int TOUCH_SDA = 1, TOUCH_SCL = 3, TOUCH_RST = 2, TOUCH_INT = 4;
static constexpr uint8_t TOUCH_ADDR = 0x58;
static uint16_t touch_x = 0, touch_y = 0;
static bool touch_pressed = false;
static uint32_t last_touch_poll = 0;

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *lcd = new Arduino_ST7789(bus, LCD_RST, 0, true, WIDTH, HEIGHT);
static lv_display_t *display;
static lv_indev_t *pointer;
static uint16_t draw_buf[WIDTH * 24];
static uint32_t last_tick;
static lv_obj_t *content;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const int w = a->x2 - a->x1 + 1;
  const int h = a->y2 - a->y1 + 1;
  lcd->draw16bitRGBBitmap(a->x1, a->y1, reinterpret_cast<uint16_t *>(px), w, h);
  lv_display_flush_ready(d);
}

static bool touch_command(uint32_t command, bool stop = true) {
  Wire1.beginTransmission(TOUCH_ADDR);
  for (int shift = 24; shift >= 0; shift -= 8) Wire1.write(uint8_t(command >> shift));
  return Wire1.endTransmission(stop) == 0;
}

static bool touch_read(uint8_t frame[9]) {
  bool ok = touch_command(0xd0070000, false);
  if (ok) ok = Wire1.requestFrom(TOUCH_ADDR, size_t(9)) == 9;
  if (ok) for (int i = 0; i < 9; ++i) frame[i] = Wire1.read();
  const bool ack = touch_command(0xd00002ab);
  return ok && ack;
}

static void touch_cb(lv_indev_t *, lv_indev_data_t *data) {
  if (millis() - last_touch_poll >= 20) {
    last_touch_poll = millis();
    uint8_t frame[9] = {};
    bool ok = touch_read(frame);
    uint8_t count = frame[3] & 15;
    uint16_t x = uint16_t(frame[4]) | (uint16_t(frame[7] & 15) << 8);
    uint16_t y = uint16_t(frame[5]) | (uint16_t(frame[7] & 0xf0) << 4);
    touch_pressed = ok && count > 0 && count <= 5 &&
                    (frame[8] & 0xf0) != 0 && x < WIDTH && y < HEIGHT;
    if (touch_pressed) { touch_x = x; touch_y = y; }
  }
  data->point.x = touch_x;
  data->point.y = touch_y;
  data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void show_page(int page) {
  lv_obj_clean(content);
  if (page == 0) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "LVGL Widgets");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *slider = lv_slider_create(content);
    lv_obj_set_width(slider, 190);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *sw = lv_switch_create(content);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 35);
  } else if (page == 1) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Controls");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *bar = lv_bar_create(content);
    lv_obj_set_width(bar, 180);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 45);
    lv_bar_set_value(bar, 65, LV_ANIM_OFF);

    lv_obj_t *slider = lv_slider_create(content);
    lv_obj_set_width(slider, 180);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 80);
    lv_slider_set_value(slider, 65, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, [](lv_event_t *e) {
      auto *source = lv_event_get_target_obj(e);
      auto *target = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
      lv_bar_set_value(target, lv_slider_get_value(source), LV_ANIM_OFF);
    }, LV_EVENT_VALUE_CHANGED, bar);

    lv_obj_t *spin = lv_spinner_create(content);
    lv_obj_set_size(spin, 36, 36);
    lv_obj_align(spin, LV_ALIGN_TOP_MID, 0, 119);
    lv_obj_t *toggle = lv_button_create(content);
    lv_obj_set_size(toggle, 96, 34);
    lv_obj_align(toggle, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_t *toggle_label = lv_label_create(toggle);
    lv_label_set_text(toggle_label, "Stop");
    lv_obj_center(toggle_label);
    lv_obj_add_event_cb(toggle, [](lv_event_t *e) {
      auto *button = lv_event_get_target_obj(e);
      auto *spinner = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
      bool running = !lv_obj_has_state(button, LV_STATE_CHECKED);
      if (running) {
        lv_obj_add_state(button, LV_STATE_CHECKED);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_t *label = lv_obj_get_child(button, 0);
      lv_label_set_text(label, running ? "Start" : "Stop");
    }, LV_EVENT_CLICKED, spin);
  } else {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "LVGL 9 Showcase\nWaveshare 2.8 V2\nST7789 240x320\nCST3530 touch enabled");
    lv_obj_center(label);
  }
}

static void nav_cb(lv_event_t *e) {
  int page = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  show_page(page);
}

void setup() {
  Serial.begin(115200);
  // Keep the battery power latch asserted (same as the working swipe branch).
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  pinMode(5, OUTPUT);
  digitalWrite(5, LOW);
  lcd->begin(40000000);
  ledcAttach(5, 20000, 8);
  ledcWrite(5, 128);
  Wire1.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire1.setTimeOut(50);
  pinMode(TOUCH_INT, INPUT);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(100);
  digitalWrite(TOUCH_RST, HIGH);
  delay(500);
  Wire1.beginTransmission(TOUCH_ADDR);
  bool detected = Wire1.endTransmission() == 0;
  Serial.printf("CST3530 V2: %s\\n", detected ? "detected" : "not detected");
  lcd->fillScreen(0x0000);
  lv_init();
  display = lv_display_create(WIDTH, HEIGHT);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, flush_cb);
  lv_display_set_buffers(display, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  pointer = lv_indev_create();
  lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer, touch_cb);

  lv_obj_t *root = lv_screen_active();
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(root, 5, 0);
  lv_obj_t *nav = lv_obj_create(root);
  lv_obj_set_size(nav, LV_PCT(100), 58);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(nav, 3, 0);
  const char *names[] = {"Widgets", "Controls", "Info"};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *btn = lv_button_create(nav);
    lv_obj_set_width(btn, 70);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, names[i]);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
  content = lv_obj_create(root);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  show_page(0);
  last_tick = millis();
  Serial.printf("LVGL showcase initialized; touch enabled: %s\n", TOUCH_READY ? "yes" : "no");
}

void loop() {
  uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;
  lv_timer_handler();
  delay(5);
}
