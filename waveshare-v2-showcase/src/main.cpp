#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <PNGdec.h>
#include <JPEGDEC.h>
#include "room_photos.h"

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


static constexpr int IMG_W = WIDTH, IMG_H = HEIGHT;
static uint16_t *scene_pixels = nullptr;
static lv_image_dsc_t scene_dsc = {};
static lv_obj_t *scene_image = nullptr;
static lv_obj_t *scene_stats = nullptr;
static lv_obj_t *scene_overlay = nullptr;
static lv_obj_t *scene_screen = nullptr;
static lv_obj_t *previous_screen = nullptr;
static bool swipe_tracking = false;
static int swipe_start_x = 0, swipe_start_y = 0;
static uint32_t swipe_start_ms = 0;
static int swipe_last_axis = 0;
static uint32_t swipe_last_axis_ms = 0;
static int swipe_velocity_px_s = 0;
static lv_obj_t *scene_title = nullptr;
static lv_obj_t *scene_old_image = nullptr;
static uint16_t *scene_old_pixels = nullptr;
static lv_image_dsc_t scene_old_dsc = {};
static int transition_type = 0;
static bool scene_animating = false;
static bool drag_active = false;
static bool drag_enabled = false;
static int drag_direction = 0;
static int drag_distance = 0;
static int drag_previous_kind = 0;
static int drag_target_kind = 0;
static uint32_t scene_switch_start = 0;

static uint32_t last_scene_ms = 0;
static int scene_kind = 0, scene_detail = 0;
static int scene_format = 0;
static uint32_t last_decode_ms = 0;
static bool last_decode_ok = true;
static PNG room_png_decoder;
static JPEGDEC room_jpg_decoder;
static bool photo_cache_enabled = false;
static uint16_t *photo_cache[2][4] = {};
static bool photo_cache_valid[2][4] = {};
static constexpr size_t PHOTO_BYTES = IMG_W * IMG_H * sizeof(uint16_t);
static bool last_scene_cached = false;

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
static int room_png_draw(PNGDRAW *line) {
  if (!scene_pixels || line->y >= IMG_H) return 0;
  static uint16_t row[IMG_W];
  room_png_decoder.getLineAsRGB565(line, row, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  memcpy(scene_pixels + line->y * IMG_W, row, IMG_W * sizeof(uint16_t));
  return 1;
}
static int room_jpg_draw(JPEGDRAW *block) {
  if (!scene_pixels) return 0;
  for (int y = 0; y < block->iHeight; ++y) {
    int py = block->y + y;
    if (py < 0 || py >= IMG_H) continue;
    for (int x = 0; x < block->iWidth; ++x) {
      int px = block->x + x;
      if (px >= 0 && px < IMG_W)
        scene_pixels[py * IMG_W + px] = block->pPixels[y * block->iWidth + x];
    }
  }
  return 1;
}
static void decode_room_photo(int kind) {
  const uint32_t started = millis();
  last_decode_ok = false;
  if (scene_format == 1) {
    const RoomPhoto &asset = room_png[kind];
    if (room_png_decoder.openRAM((uint8_t *)asset.data, asset.size, room_png_draw) == PNG_SUCCESS) {
      last_decode_ok = room_png_decoder.decode(nullptr, 0) == PNG_SUCCESS;
      room_png_decoder.close();
    }
  } else if (scene_format == 2) {
    const RoomPhoto &asset = room_jpg[kind];
    if (room_jpg_decoder.openRAM((uint8_t *)asset.data, asset.size, room_jpg_draw)) {
      room_jpg_decoder.setPixelType(RGB565_LITTLE_ENDIAN);
      last_decode_ok = room_jpg_decoder.decode(0, 0, 0);
      room_jpg_decoder.close();
    }
  }
  last_decode_ms = millis() - started;
  last_scene_ms = last_decode_ms;
  if (scene_image) lv_obj_invalidate(scene_image);
}
static void render_scene(int kind, int detail) {
  if (!scene_pixels) return;
  last_scene_cached = false;
  if (scene_format != 0) {
    const int format = scene_format - 1;
    if (photo_cache_enabled && photo_cache_valid[format][kind]) {
      const uint32_t start = micros();
      memcpy(scene_pixels, photo_cache[format][kind], PHOTO_BYTES);
      last_scene_ms = (micros() - start + 999) / 1000;
      last_decode_ok = true;
      last_scene_cached = true;
      if (scene_image) lv_obj_invalidate(scene_image);
      return;
    }
    decode_room_photo(kind);
    if (photo_cache_enabled && last_decode_ok) {
      if (!photo_cache[format][kind])
        photo_cache[format][kind] = static_cast<uint16_t *>(ps_malloc(PHOTO_BYTES));
      if (photo_cache[format][kind]) {
        memcpy(photo_cache[format][kind], scene_pixels, PHOTO_BYTES);
        photo_cache_valid[format][kind] = true;
      } else Serial.printf("[CACHE] Allocation failed: %d/%d\n", format, kind);
    }
    return;
  }
  const uint32_t started = micros();
  uint8_t r = 0, g = 0, b = 0;
  for (int y = 0; y < IMG_H; ++y) for (int x = 0; x < IMG_W; ++x) {
    if (kind == 0) { r = y < 224 ? 108 : 84; g = y < 224 ? 96 : 69; b = y < 224 ? 85 : 58; }
    if (kind == 1) { r = y < 224 ? 102 : 75; g = y < 224 ? 126 : 86; b = y < 224 ? 145 : 100; }
    if (kind == 2) { r = y < 224 ? 221 : 156; g = y < 224 ? 207 : 137; b = y < 224 ? 175 : 107; }
    if (kind == 3) { r = y < 210 ? 112 : 87; g = y < 210 ? 167 : 138; b = y < 210 ? 191 : 86; }
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
    "RGB565 %dx%d | %lu KB | Gen %lu ms\nFlush %lu calls, %lu px, %lu us",
    IMG_W, IMG_H, (unsigned long)(IMG_W * IMG_H * 2 / 1024),
    (unsigned long)last_scene_ms, (unsigned long)flush_calls,
    (unsigned long)flush_pixels, (unsigned long)flush_us);
}
static void close_scene_overlay() {
  if (!scene_overlay) return;
  lv_screen_load(previous_screen);
  lv_obj_delete(scene_screen);
  scene_screen = nullptr;
  previous_screen = nullptr;
  scene_overlay = nullptr;
  swipe_tracking = false;
  scene_image = nullptr;
  scene_title = nullptr;
  scene_old_image = nullptr;
  scene_animating = false;
  drag_active = false;
}
static void scene_anim_exec(void *obj, int32_t value) {
  if (transition_type == 2) lv_obj_set_y(static_cast<lv_obj_t *>(obj), value);
  else lv_obj_set_x(static_cast<lv_obj_t *>(obj), value);
}
static void scene_fade_exec(void *obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), (lv_opa_t)value, 0);
}
static void scene_update_title() {
  if (!scene_title) return;
  const char *names[] = {"Entertainment", "Bedroom", "Kitchen", "Patio"};
  lv_label_set_text_fmt(scene_title, "%s | %s %lu ms", names[scene_kind], scene_format == 0 ? "Gen" : (last_scene_cached ? "Cache" : (last_decode_ok ? "Decode" : "Decode ERR")), (unsigned long)last_scene_ms);
}
static void scene_finish(bool commit) {
  if (scene_old_image) {
    lv_anim_delete(scene_old_image, nullptr);
    lv_obj_delete(scene_old_image);
    scene_old_image = nullptr;
  }
  if (!commit && scene_old_pixels) {
    memcpy(scene_pixels, scene_old_pixels, IMG_W * IMG_H * sizeof(uint16_t));
    scene_kind = drag_previous_kind;
    lv_obj_invalidate(scene_image);
  }
  if (scene_image) {
    lv_obj_set_pos(scene_image, 0, 0);
    lv_obj_set_style_opa(scene_image, LV_OPA_COVER, 0);
  }
  scene_update_title();
  drag_active = false;
  scene_animating = false;
}
static void scene_anim_ready(lv_anim_t *) {
  scene_finish(true);
}
static void scene_cancel_ready(lv_anim_t *) {
  scene_finish(false);
}
static void scene_animate_obj(lv_obj_t *obj, int from, int to, uint32_t duration,
                              bool fade, lv_anim_ready_cb_t ready = nullptr) {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_duration(&anim, duration);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_set_values(&anim, from, to);
  lv_anim_set_exec_cb(&anim, fade ? scene_fade_exec : scene_anim_exec);
  if (ready) lv_anim_set_ready_cb(&anim, ready);
  lv_anim_start(&anim);
}
static bool scene_prepare(int direction) {
  if (scene_animating || drag_active || !scene_old_pixels) return false;
  memcpy(scene_old_pixels, scene_pixels, IMG_W * IMG_H * sizeof(uint16_t));
  drag_previous_kind = scene_kind;
  drag_target_kind = (scene_kind + direction + 4) % 4;
  scene_old_image = lv_image_create(scene_overlay);
  lv_image_set_src(scene_old_image, &scene_old_dsc);
  lv_obj_set_pos(scene_old_image, 0, 0);
  lv_obj_move_to_index(scene_old_image, 0);
  scene_kind = drag_target_kind;
  render_scene(scene_kind, scene_detail);
  scene_update_title();
  drag_direction = direction;
  drag_distance = 0;
  drag_active = true;
  return true;
}
static void scene_drag_position(int distance) {
  if (!drag_active) return;
  const bool vertical = transition_type == 2;
  const int extent = vertical ? HEIGHT : WIDTH;
  const int sign = drag_direction > 0 ? -1 : 1;
  distance = constrain(distance, 0, extent);
  drag_distance = distance;
  if (vertical) {
    lv_obj_set_y(scene_old_image, sign * distance);
    lv_obj_set_y(scene_image, sign * (distance - extent));
  } else {
    lv_obj_set_x(scene_old_image, sign * distance);
    lv_obj_set_x(scene_image, sign * (distance - extent));
  }
}
static void scene_release_drag(int velocity_px_s) {
  if (!drag_active || scene_animating) return;
  const int extent = transition_type == 2 ? HEIGHT : WIDTH;
  const int sign = drag_direction > 0 ? -1 : 1;
  // A deliberate fast flick commits even when travel is below 35%.
  // Velocity is signed in the direction of the intended page change.
  const bool fast_flick = velocity_px_s >= 450 && drag_distance >= 12;
  const bool commit = drag_distance >= extent * 35 / 100 || fast_flick;
  const int remaining = commit ? extent - drag_distance : drag_distance;
  const uint32_t duration = uint32_t(220) * remaining / extent + 45;
  const int old_end = commit ? sign * extent : 0;
  const int new_end = commit ? 0 : -sign * extent;
  scene_animating = true;
  // Start the old image first; the new image's completion callback performs cleanup.
  scene_animate_obj(scene_old_image, sign * drag_distance, old_end, duration, false);
  scene_animate_obj(scene_image, sign * (drag_distance - extent), new_end,
                    duration, false, commit ? scene_anim_ready : scene_cancel_ready);
}
static void full_scene_next(int direction) {
  if (scene_animating || drag_active) return;
  if (direction == 0) { render_scene(scene_kind, scene_detail); scene_update_title(); return; }
  if (!scene_old_pixels) {
    scene_kind = (scene_kind + direction + 4) % 4;
    render_scene(scene_kind, scene_detail);
    scene_update_title();
    return;
  }
  if (!scene_prepare(direction)) return;
  if (transition_type == 0) {
    scene_finish(true);
  } else if (transition_type == 3) {
    // Fade OUT the old image above the new image, revealing it gradually.
    scene_animating = true;
    lv_obj_move_to_index(scene_old_image, 1); // old image above new, controls stay above both
    lv_obj_set_style_opa(scene_old_image, LV_OPA_COVER, 0);
    scene_animate_obj(scene_old_image, 255, 0, 350, true, scene_anim_ready);
  } else {
    const int extent = transition_type == 2 ? HEIGHT : WIDTH;
    const int sign = direction > 0 ? -1 : 1;
    scene_animating = true;
    if (transition_type == 2) lv_obj_set_y(scene_image, -sign * extent);
    else lv_obj_set_x(scene_image, -sign * extent);
    scene_animate_obj(scene_old_image, 0, sign * extent, 320, false);
    scene_animate_obj(scene_image, -sign * extent, 0, 320, false, scene_anim_ready);
  }
}
static void preload_room_cache() {
  if (!photo_cache_enabled || scene_format == 0 || !scene_pixels) return;
  const int original_kind = scene_kind;
  const int format = scene_format - 1;
  const uint32_t start = millis();
  int loaded = 0;
  for (int kind = 0; kind < 4; ++kind) {
    if (!photo_cache_valid[format][kind]) {
      render_scene(kind, scene_detail);
      if (photo_cache_valid[format][kind]) ++loaded;
    }
  }
  render_scene(original_kind, scene_detail);
  Serial.printf("[CACHE] %s: %d decoded in %lu ms; free PSRAM %lu\n",
    scene_format == 1 ? "PNG" : "JPEG", loaded,
    (unsigned long)(millis() - start), (unsigned long)ESP.getFreePsram());
}
static void open_full_scene() {
  if (scene_overlay || !scene_pixels) return;
  previous_screen = lv_screen_active();
  scene_screen = lv_obj_create(nullptr);
  lv_obj_set_style_pad_all(scene_screen, 0, 0);
  lv_obj_set_style_border_width(scene_screen, 0, 0);
  scene_overlay = lv_obj_create(scene_screen);
  lv_obj_set_size(scene_overlay, WIDTH, HEIGHT);
  lv_obj_set_pos(scene_overlay, 0, 0);
  lv_obj_set_style_pad_all(scene_overlay, 0, 0);
  lv_obj_set_style_border_width(scene_overlay, 0, 0);
  lv_obj_set_style_radius(scene_overlay, 0, 0);
  lv_obj_remove_flag(scene_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scene_overlay, LV_OBJ_FLAG_CLICKABLE);
  scene_image = lv_image_create(scene_overlay);
  lv_image_set_src(scene_image, &scene_dsc);
  lv_obj_set_pos(scene_image, 0, 0);
  lv_obj_add_flag(scene_image, LV_OBJ_FLAG_CLICKABLE);
  scene_title = lv_label_create(scene_overlay);
  lv_obj_set_style_text_color(scene_title, lv_color_white(), 0);
  lv_obj_set_style_bg_color(scene_title, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scene_title, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(scene_title, 5, 0);
  lv_obj_align(scene_title, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_t *mode = lv_dropdown_create(scene_overlay);
  lv_dropdown_set_options(mode, "Instant\nSlide horizontal\nSlide vertical\nFade");
  lv_obj_set_width(mode, 123);
  lv_obj_align(mode, LV_ALIGN_TOP_LEFT, 5, 6);
  lv_dropdown_set_selected(mode, transition_type);
  lv_obj_add_event_cb(mode, [](lv_event_t *e) {
    transition_type = lv_dropdown_get_selected(lv_event_get_target_obj(e));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *tracking = lv_dropdown_create(scene_overlay);
  lv_dropdown_set_options(tracking, "Release swipe\nTrack finger");
  lv_obj_set_width(tracking, 118);
  lv_obj_align(tracking, LV_ALIGN_TOP_LEFT, 5, 51);
  lv_dropdown_set_selected(tracking, drag_enabled ? 1 : 0);
  lv_obj_add_event_cb(tracking, [](lv_event_t *e) {
    drag_enabled = lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1;
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *exit_button = lv_button_create(scene_overlay);
  lv_obj_set_size(exit_button, 55, 31);
  lv_obj_align(exit_button, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_t *exit_label = lv_label_create(exit_button);
  lv_label_set_text(exit_label, "Exit");
  lv_obj_center(exit_label);
  lv_obj_add_event_cb(exit_button, [](lv_event_t *) { close_scene_overlay(); }, LV_EVENT_CLICKED, nullptr);
  full_scene_next(0);
  preload_room_cache();
  scene_update_title();
  lv_screen_load(scene_screen);
}
static void show_page(int page) {
  close_scene_overlay();
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
        Serial.printf("List selected: %s\n", lv_label_get_text(lv_obj_get_child(btn, 0)));
      }, LV_EVENT_CLICKED, nullptr);
    }
  } else if (page >= 4 && page <= 6) {
    scene_format = page - 4;
    lv_obj_t *intro = lv_label_create(content);
    lv_label_set_text(intro, scene_format == 0 ? "Generated RGB565 rooms\nSwipe to switch scenes" : (scene_format == 1 ? "Photographic PNG rooms\nDecoded on ESP32-S3" : "Photographic JPEG rooms\nDecoded on ESP32-S3"));
    lv_obj_align(intro, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *launch = lv_button_create(content);
    lv_obj_set_size(launch, 170, 48);
    lv_obj_align(launch, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *launch_label = lv_label_create(launch);
    lv_label_set_text(launch_label, scene_format == 0 ? "Open scenes" : (scene_format == 1 ? "Open PNG scenes" : "Open JPEG scenes"));
    lv_obj_center(launch_label);
    lv_obj_add_event_cb(launch, [](lv_event_t *) { open_full_scene(); }, LV_EVENT_CLICKED, nullptr);
    if (scene_format != 0) {
      lv_obj_t *cache_mode = lv_dropdown_create(content);
      lv_dropdown_set_options(cache_mode, "Decode each swipe\nCache in PSRAM");
      lv_obj_set_width(cache_mode, 190);
      lv_obj_align(cache_mode, LV_ALIGN_BOTTOM_MID, 0, -12);
      lv_dropdown_set_selected(cache_mode, photo_cache_enabled ? 1 : 0);
      lv_obj_add_event_cb(cache_mode, [](lv_event_t *e) {
        photo_cache_enabled = lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1;
      }, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    if (scene_format == 0) {
    lv_obj_t *detail = lv_dropdown_create(content);
    lv_dropdown_set_options(detail, "Flat\nTexture\nNoise");
    lv_obj_set_width(detail, 170);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_dropdown_set_selected(detail, scene_detail);
    lv_obj_add_event_cb(detail, [](lv_event_t *e) {
      scene_detail = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
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
  Serial.printf("CST3530 V2: %s\n", detected ? "detected" : "not detected");
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
  const char *names[] = {"Widgets", "Controls", "Chart", "List", "Scenes", "PNG", "JPEG", "Diag"};
  for (int i = 0; i < 8; ++i) {
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
  scene_old_pixels = static_cast<uint16_t *>(ps_malloc(IMG_W * IMG_H * sizeof(uint16_t)));
  if (scene_old_pixels) {
    scene_old_dsc = scene_dsc;
    scene_old_dsc.data = reinterpret_cast<const uint8_t *>(scene_old_pixels);
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
  // Track raw CST3530 coordinates, including motion back toward the start.
  if (scene_screen) {
    if (touch_pressed && !swipe_tracking && !scene_animating) {
      swipe_tracking = true;
      swipe_start_x = touch_x;
      swipe_start_y = touch_y;
      swipe_start_ms = now;
      swipe_last_axis = transition_type == 2 ? touch_y : touch_x;
      swipe_last_axis_ms = now;
      swipe_velocity_px_s = 0;
    } else if (touch_pressed && swipe_tracking && drag_enabled &&
               (transition_type == 1 || transition_type == 2)) {
      const bool vertical = transition_type == 2;
      int delta = vertical ? int(touch_y) - swipe_start_y : int(touch_x) - swipe_start_x;
      int cross = vertical ? int(touch_x) - swipe_start_x : int(touch_y) - swipe_start_y;
      if (!drag_active && !scene_animating && swipe_start_y > 88 &&
          abs(delta) >= 12 && abs(delta) > abs(cross) * 3 / 2) {
        scene_prepare(delta < 0 ? 1 : -1);
      }
      const int axis = vertical ? touch_y : touch_x;
      if (now > swipe_last_axis_ms && axis != swipe_last_axis) {
        const int instantaneous = (axis - swipe_last_axis) * 1000 / int(now - swipe_last_axis_ms);
        // Smooth the latest movement without requiring a long finger hold.
        swipe_velocity_px_s = (swipe_velocity_px_s + instantaneous * 2) / 3;
        swipe_last_axis = axis;
        swipe_last_axis_ms = now;
      }
      if (drag_active) {
        const int intended_sign = drag_direction > 0 ? -1 : 1;
        scene_drag_position(delta * intended_sign);
      }
    } else if (!touch_pressed && swipe_tracking) {
      int dx = int(touch_x) - swipe_start_x;
      int dy = int(touch_y) - swipe_start_y;
      uint32_t duration = now - swipe_start_ms;
      swipe_tracking = false;
      if (drag_active) {
        const int intended_sign = drag_direction > 0 ? -1 : 1;
        int velocity = swipe_velocity_px_s * intended_sign;
        // Fast short flicks can finish; reversing direction gives negative velocity.
        scene_release_drag(velocity);
      }
      else if (!scene_animating && duration < 1200 && swipe_start_y > 88) {
        if (transition_type == 2 && abs(dy) >= 45 && abs(dy) > abs(dx) * 3 / 2)
          full_scene_next(dy < 0 ? 1 : -1);
        else if (transition_type != 2 && abs(dx) >= 45 && abs(dx) > abs(dy) * 3 / 2)
          full_scene_next(dx < 0 ? 1 : -1);
      }
    }
  } else swipe_tracking = false;
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
