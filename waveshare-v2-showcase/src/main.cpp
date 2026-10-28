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

static uint32_t flush_pixels = 0, flush_calls = 0, flush_us = 0;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const int w = a->x2 - a->x1 + 1;
  const int h = a->y2 - a->y1 + 1;
  uint32_t start_us = micros();
  lcd->draw16bitRGBBitmap(a->x1, a->y1, reinterpret_cast<uint16_t *>(px), w, h);
  flush_us += micros() - start_us;
  flush_pixels += uint32_t(w) * h;
  ++flush_calls;
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


static constexpr int IMG_W = 192, IMG_H = 160;
static uint16_t *scene_pixels = nullptr;
static lv_image_dsc_t scene_dsc = {};
static lv_obj_t *scene_image = nullptr;
static lv_obj_t *scene_stats = nullptr;

static uint32_t last_scene_ms = 0;
static int scene_kind = 0, scene_detail = 0;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t((r & 0xf8) << 8) | uint16_t((g & 0xfc) << 3) | (b >> 3);
}
static void rect_pixels(int x0, int y0, int x1, int y1, uint16_t c) {
  if (!scene_pixels) return;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > IMG_W) x1 = IMG_W;
  if (y1 > IMG_H) y1 = IMG_H;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) scene_pixels[y * IMG_W + x] = c;
}
static uint32_t hash_pixel(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t v = x * 374761393UL + y * 668265263UL + seed * 2246822519UL;
  v = (v ^ (v >> 13)) * 1274126177UL;
  return v ^ (v >> 16);
}
static void render_scene(int kind, int detail) {
  if (!scene_pixels) return;
  const uint32_t started = micros();
  uint8_t r = 0, g = 0, b = 0;
  for (int y = 0; y < IMG_H; ++y) for (int x = 0; x < IMG_W; ++x) {
    if (kind == 0) { r = y < 112 ? 108 : 84; g = y < 112 ? 96 : 69; b = y < 112 ? 85 : 58; }
    if (kind == 1) { r = y < 112 ? 102 : 75; g = y < 112 ? 126 : 86; b = y < 112 ? 145 : 100; }
    if (kind == 2) { r = y < 112 ? 221 : 156; g = y < 112 ? 207 : 137; b = y < 112 ? 175 : 107; }
    if (kind == 3) { r = y < 105 ? 112 : 87; g = y < 105 ? 167 : 138; b = y < 105 ? 191 : 86; }
    if (detail) {
      uint32_t noise = hash_pixel(x, y, kind);
      int delta = detail == 1 ? int(noise % 17) - 8 : int(noise % 97) - 48;
      r = uint8_t(constrain(int(r) + delta, 0, 255));
      g = uint8_t(constrain(int(g) + delta, 0, 255));
      b = uint8_t(constrain(int(b) + delta, 0, 255));
    }
    scene_pixels[y * IMG_W + x] = rgb565(r, g, b);
  }
  if (kind == 0) { // Entertainment center: TV, console, speakers
    rect_pixels(18, 32, 174, 112, rgb565(43, 35, 31));
    rect_pixels(38, 39, 155, 100, rgb565(9, 18, 31));
    rect_pixels(43, 44, 150, 95, rgb565(25, 55, 80));
    rect_pixels(22, 116, 170, 130, rgb565(48, 30, 20));
    rect_pixels(25, 53, 34, 108, rgb565(18, 17, 18));
    rect_pixels(161, 53, 170, 108, rgb565(18, 17, 18));
  } else if (kind == 1) { // Bedroom: window, bed, pillows
    rect_pixels(17, 15, 76, 86, rgb565(225, 226, 217));
    rect_pixels(23, 20, 70, 80, rgb565(97, 167, 198));
    rect_pixels(83, 74, 178, 137, rgb565(74, 55, 68));
    rect_pixels(88, 83, 173, 132, rgb565(202, 188, 175));
    rect_pixels(91, 85, 128, 102, rgb565(240, 232, 214));
    rect_pixels(132, 85, 169, 102, rgb565(240, 232, 214));
  } else if (kind == 2) { // Kitchen: cabinets, backsplash, counter
    rect_pixels(12, 18, 181, 65, rgb565(137, 91, 49));
    for (int x = 17; x < 177; x += 40) rect_pixels(x, 22, x + 35, 59, rgb565(183, 138, 84));
    rect_pixels(11, 90, 182, 102, rgb565(63, 66, 65));
    rect_pixels(11, 102, 182, 150, rgb565(149, 101, 59));
    rect_pixels(77, 68, 118, 91, rgb565(183, 188, 185));
    rect_pixels(87, 73, 108, 85, rgb565(90, 95, 98));
  } else { // Patio: sky, fence, planters, table
    rect_pixels(0, 78, 192, 101, rgb565(102, 119, 79));
    for (int x = 0; x < 192; x += 24) rect_pixels(x, 89, x + 19, 123, rgb565(132, 92, 61));
    rect_pixels(38, 113, 155, 121, rgb565(64, 53, 42));
    rect_pixels(85, 120, 94, 157, rgb565(64, 53, 42));
    rect_pixels(15, 114, 39, 143, rgb565(107, 67, 42));
    rect_pixels(19, 102, 35, 117, rgb565(35, 115, 55));
    rect_pixels(157, 114, 180, 143, rgb565(107, 67, 42));
    rect_pixels(160, 101, 178, 117, rgb565(35, 115, 55));
  }
  last_scene_ms = (micros() - started) / 1000;
  if (scene_image) lv_obj_invalidate(scene_image);
}
static void update_scene_stats() {
  if (!scene_stats) return;
  lv_label_set_text_fmt(scene_stats,
    "RGB565 %dx%d | %lu KB | Gen %lu ms\\nFlush %lu calls, %lu px, %lu us",
    IMG_W, IMG_H, (unsigned long)(IMG_W * IMG_H * 2 / 1024),
    (unsigned long)last_scene_ms, (unsigned long)flush_calls,
    (unsigned long)flush_pixels, (unsigned long)flush_us);
}
static void select_scene(int kind) {
  scene_kind = kind;
  flush_calls = flush_pixels = flush_us = 0;
  render_scene(scene_kind, scene_detail);
  update_scene_stats();
}
static void show_page(int page) {
  current_page = page;
  diag_label = nullptr;
  scene_image = nullptr;
  scene_stats = nullptr;
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
    lv_obj_t *viewport = lv_obj_create(content);
    lv_obj_set_size(viewport, 200, 132);
    lv_obj_align(viewport, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_scroll_dir(viewport, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_t *chart = lv_chart_create(viewport);
    lv_obj_set_size(chart, 570, 125);
    lv_obj_set_pos(chart, 0, 0);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 60);
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
  } else if (page == 4) {
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Scene image benchmark");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 1);
    if (!scene_pixels) {
      lv_obj_t *err = lv_label_create(content);
      lv_label_set_text(err, "Image allocation failed");
      lv_obj_center(err);
    } else {
      scene_image = lv_image_create(content);
      lv_image_set_src(scene_image, &scene_dsc);
      lv_obj_align(scene_image, LV_ALIGN_TOP_MID, 0, 24);
      lv_obj_t *prev = lv_button_create(content);
      lv_obj_set_size(prev, 49, 29);
      lv_obj_align(prev, LV_ALIGN_BOTTOM_LEFT, 1, -35);
      lv_obj_t *pl = lv_label_create(prev);
      lv_label_set_text(pl, "<");
      lv_obj_center(pl);
      lv_obj_add_event_cb(prev, [](lv_event_t *) { select_scene((scene_kind + 3) % 4); }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *next = lv_button_create(content);
      lv_obj_set_size(next, 49, 29);
      lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -1, -35);
      lv_obj_t *nl = lv_label_create(next);
      lv_label_set_text(nl, ">");
      lv_obj_center(nl);
      lv_obj_add_event_cb(next, [](lv_event_t *) { select_scene((scene_kind + 1) % 4); }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *detail = lv_dropdown_create(content);
      lv_dropdown_set_options(detail, "Flat\\nTexture\\nNoise");
      lv_obj_set_width(detail, 90);
      lv_obj_align(detail, LV_ALIGN_BOTTOM_MID, 0, -35);
      lv_dropdown_set_selected(detail, scene_detail);
      lv_obj_add_event_cb(detail, [](lv_event_t *e) {
        scene_detail = lv_dropdown_get_selected(lv_event_get_target_obj(e));
        select_scene(scene_kind);
      }, LV_EVENT_VALUE_CHANGED, nullptr);
      scene_stats = lv_label_create(content);
      lv_obj_set_width(scene_stats, 200);
      lv_obj_align(scene_stats, LV_ALIGN_BOTTOM_MID, 0, -1);
      select_scene(scene_kind);
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
  const char *names[] = {"Widgets", "Controls", "Chart", "List", "Scenes", "Diag"};
  for (int i = 0; i < 6; ++i) {
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
  scene_pixels = static_cast<uint16_t *>(ps_malloc(IMG_W * IMG_H * sizeof(uint16_t)));
  if (!scene_pixels) scene_pixels = static_cast<uint16_t *>(malloc(IMG_W * IMG_H * sizeof(uint16_t)));
  if (scene_pixels) {
    scene_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    scene_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    scene_dsc.header.w = IMG_W;
    scene_dsc.header.h = IMG_H;
    scene_dsc.data_size = IMG_W * IMG_H * sizeof(uint16_t);
    scene_dsc.data = reinterpret_cast<const uint8_t *>(scene_pixels);
    render_scene(0, 0);
  }
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
