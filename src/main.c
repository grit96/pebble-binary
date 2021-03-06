#include <pebble.h>
#include "settings.c"

enum {
  SCREEN_WIDTH = 144,
  SCREEN_HEIGHT = 168,

  CIRCLE_LINE_THICKNESS = 2,
  CIRCLE_PADDING = 2,
  SIDE_PADDING = 12,

  HOURS_MAX_COLS = 4,
  MINUTES_MAX_COLS = 6,
  HOURS_ROW_START = SIDE_PADDING,
  MINUTES_ROW_START = 4 * SIDE_PADDING
};

static bool BATTERY_PERCENTAGE = true;
static bool SHOW_DATE = true;
static bool SHOW_WEATHER = true;
static bool INVERT_COLOURS = false;
static bool BLUETOOTH_VIBRATE = false;
static bool HOURLY_VIBRATE = false;

bool bluetooth_connected = true;

static Window *s_main_window;
static Layer *s_display_layer;
static TextLayer *s_date_layer, *s_weather_layer, *s_battery_layer;

static char weather_layer_buffer[32];


static void save_settings(DictionaryIterator *iter) {
  save_setting(iter, MESSAGE_KEY_BATTERY_PERCENTAGE);
  save_setting(iter, MESSAGE_KEY_SHOW_DATE);
  save_setting(iter, MESSAGE_KEY_SHOW_WEATHER);
  save_setting(iter, MESSAGE_KEY_INVERT_COLOURS);
  save_setting(iter, MESSAGE_KEY_BLUETOOTH_VIBRATE);
  save_setting(iter, MESSAGE_KEY_HOURLY_VIBRATE);
}

static void update_settings() {
  BATTERY_PERCENTAGE = load_setting(MESSAGE_KEY_BATTERY_PERCENTAGE, BATTERY_PERCENTAGE);
  SHOW_DATE = load_setting(MESSAGE_KEY_SHOW_DATE, SHOW_DATE);
  SHOW_WEATHER = load_setting(MESSAGE_KEY_SHOW_WEATHER, SHOW_WEATHER);
  INVERT_COLOURS = load_setting(MESSAGE_KEY_INVERT_COLOURS, INVERT_COLOURS);
  BLUETOOTH_VIBRATE = load_setting(MESSAGE_KEY_BLUETOOTH_VIBRATE, BLUETOOTH_VIBRATE);
  HOURLY_VIBRATE = load_setting(MESSAGE_KEY_HOURLY_VIBRATE, HOURLY_VIBRATE);
}

static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100% charged";

  if (charge_state.is_charging) {
    snprintf(battery_text, sizeof(battery_text), "charging");
  } else {
    snprintf(battery_text, sizeof(battery_text), "%d%% charged", charge_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, battery_text);
}

static void handle_bluetooth(bool connected) {
  if (BLUETOOTH_VIBRATE && connected != bluetooth_connected) {
    bluetooth_connected = connected;
    vibes_double_pulse();
  }
}

static void draw_cell(GContext *ctx, GPoint center, short radius, bool filled) {
  graphics_context_set_fill_color(ctx, INVERT_COLOURS ? GColorBlack : GColorWhite);
  graphics_fill_circle(ctx, center, radius);

  if (!filled) {
    graphics_context_set_fill_color(ctx, INVERT_COLOURS ? GColorWhite : GColorBlack);
    graphics_fill_circle(ctx, center, radius - CIRCLE_LINE_THICKNESS);
  }
}

static GPoint get_cell_centre(short x, short y, short radius) {
  short cell_size = (2 * (radius + CIRCLE_PADDING));

  return GPoint(SCREEN_WIDTH - (SIDE_PADDING + (cell_size / 2) + (cell_size * x)), (cell_size / 2) + y);
}

static void draw_cell_row_for_digit(GContext *ctx, short digit, short max_cols, short cell_row) {
  short radius = (((SCREEN_WIDTH - (2 * SIDE_PADDING)) / max_cols) - (2 * CIRCLE_PADDING)) / 2;

  for (int i = 0; i < max_cols; i++) {
    draw_cell(ctx, get_cell_centre(i, cell_row, radius), radius, (digit >> i) & 0x1);
  }
}

static short get_display_hour(short hour) {
  if (clock_is_24h_style()) {
    return hour;
  }

  return hour % 12 || 12;
}

static void display_layer_update_callback(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  short display_hour = get_display_hour(t->tm_hour);
  short hours_bits = HOURS_MAX_COLS + (clock_is_24h_style() ? 1 : 0);

  int PADDING = (SHOW_DATE == 0 && SHOW_WEATHER == 0) ? 40 : 0;

  draw_cell_row_for_digit(ctx, display_hour, hours_bits, HOURS_ROW_START + PADDING);
  draw_cell_row_for_digit(ctx, t->tm_min, MINUTES_MAX_COLS, MINUTES_ROW_START + PADDING);
}

static void update_time() {
  static char date_layer_buffer[32];

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  layer_mark_dirty(s_display_layer);

  strftime(date_layer_buffer, sizeof(date_layer_buffer), "%B %e", t);
  text_layer_set_text(s_date_layer, date_layer_buffer);

  // Update weather every 30 minutes
  if (t->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
  }

  if (HOURLY_VIBRATE && t->tm_min % 60 == 0) {
    vibes_short_pulse();
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  update_settings();

  window_set_background_color(window, INVERT_COLOURS ? GColorWhite : GColorBlack);

  s_display_layer = layer_create(bounds);
  layer_set_update_proc(s_display_layer, display_layer_update_callback);
  layer_add_child(window_layer, s_display_layer);

  s_date_layer = text_layer_create(GRect(0, 80, SCREEN_WIDTH, 30));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, INVERT_COLOURS ? GColorBlack : GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  layer_set_hidden(text_layer_get_layer(s_date_layer), SHOW_DATE == 0);

  s_weather_layer = text_layer_create(GRect(0, 110, SCREEN_WIDTH, 25));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, INVERT_COLOURS ? GColorBlack : GColorWhite);
  text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  text_layer_set_text(s_weather_layer, weather_layer_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));
  layer_set_hidden(text_layer_get_layer(s_weather_layer), SHOW_WEATHER == 0);

  s_battery_layer = text_layer_create(GRect(0, 140, SCREEN_WIDTH, 20));
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_text_color(s_battery_layer, INVERT_COLOURS ? GColorBlack : GColorWhite);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));
  layer_set_hidden(text_layer_get_layer(s_battery_layer), BATTERY_PERCENTAGE == 0);

  // Get initial battery percentage and bluetooth state
  handle_battery(battery_state_service_peek());
  bluetooth_connected = bluetooth_connection_service_peek();
  update_time();
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weather_layer);
  text_layer_destroy(s_battery_layer);
  layer_destroy(s_display_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  static char temperature_buffer[8];
  static char conditions_buffer[32];

  Tuple *temperature = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  if (temperature) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Temperature: %d", (int)temperature->value->int32);
    snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", (int)temperature->value->int32);
  }

  Tuple *conditions = dict_find(iterator, MESSAGE_KEY_CONDITIONS);
  if (conditions) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Conditions: %s", conditions->value->cstring);
    snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", conditions->value->cstring);
  }

  snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "%s, %s", temperature_buffer, conditions_buffer);

  save_settings(iterator);
  main_window_unload(s_main_window);
  main_window_load(s_main_window);
}

static void init() {
  snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "Loading...");

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  // Subscribe service handlers
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(handle_battery);
  bluetooth_connection_service_subscribe(handle_bluetooth);

  // Set up listeners for PebbleKit JS
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
