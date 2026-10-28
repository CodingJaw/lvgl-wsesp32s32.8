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

static lv_obj_t *diag_label = nullptr;
static uint32_t diag_last = 0;
static uint32_t diag_frames = 0;
static int current_page = 0;

static void show_page(int page) {
  current_page = page;
  diag_label = nullptr;
  lv_obj_clean(content);
  if (page == 0) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Widgets");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *slider = lv_slider_create(content);
    lv_obj_set_width(slider, 175);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 43);
    lv_obj_t *sw = lv_switch_create(content);
    lv_obj_align(sw, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_t *dropdown = lv_dropdown_create(content);
    lv_dropdown_set_options(dropdown, "Normal\nEco\nPerformance");
    lv_obj_set_width(dropdown, 180);
    lv_obj_align(dropdown, LV_ALIGN_TOP_MID, 0, 125);
  } else if (page == 1) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Controls");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);
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
      bool stop = !lv_obj_has_state(button, LV_STATE_CHECKED);
      if (stop) {
        lv_obj_add_state(button, LV_STATE_CHECKED);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      }
      lv_label_set_text(lv_obj_get_child(button, 0), stop ? "Start" : "Stop");
    }, LV_EVENT_CLICKED, spin);
  } else if (page == 2) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Chart (drag to scroll)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *chart = lv_chart_create(content);
    lv_obj_set_size(chart, 195, 125);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 32);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 60);
    lv_chart_set_zoom_x(chart, 768);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(chart, LV_DIR_HOR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 5, 5);
    lv_chart_series_t *series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 60; ++i) lv_chart_set_next_value(chart, series, 40 + (i * 17 + i * i * 3) % 55);
    lv_chart_refresh(chart);
    lv_obj_t *arc = lv_arc_create(content);
    lv_obj_set_size(arc, 62, 62);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 45);
  } else if (page == 3) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Scrollable list");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_t *list = lv_list_create(content);
    lv_obj_set_size(list, 190, 166);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 30);
    const char *items[] = {"Living room", "Bedroom", "Office", "TV 1", "TV 2", "TV 3", "IR router", "USB HID"};
    for (const char *item : items) {
      lv_obj_t *btn = lv_list_add_button(list, nullptr, item);
      lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_obj_t *btn = lv_event_get_target_obj(e);
        Serial.printf("List selected: %s\\n", lv_label_get_text(lv_obj_get_child(btn, 0)));
      }, LV_EVENT_CLICKED, nullptr);
    }
  } else {
    diag_label = lv_label_create(content);
    lv_obj_align(diag_label, LV_ALIGN_TOP_LEFT, 5, 8);
    lv_label_set_text(diag_label, "Diagnostics loading...");
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
  lv_obj_add_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(nav, LV_DIR_HOR);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(nav, 3, 0);
  const char *names[] = {"Widgets", "Controls", "Chart", "List", "Diag"};
  for (int i = 0; i < 5; ++i) {
    lv_obj_t *btn = lv_button_create(nav);
    lv_obj_set_width(btn, 74);
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
  ++diag_frames;
  if (now - diag_last >= 1000) {
    if (diag_label) {
      const uint32_t elapsed = now - diag_last;
      const uint32_t loops_per_second = elapsed ? diag_frames * 1000UL / elapsed : 0;
      lv_label_set_text_fmt(diag_label,
        "Waveshare 2.8 V2\nLVGL 9.2.2\nUptime: %lu s\nLoop: %lu/s\nHeap: %lu B\nPSRAM: %lu B\nTouch: %u, %u (%s)",
        (unsigned long)(now / 1000), (unsigned long)loops_per_second,
        (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram(),
        touch_x, touch_y, touch_pressed ? "down" : "up");
    }
    diag_last = now;
    diag_frames = 0;
  }
  delay(5);
}
