/*
########################## open include directory and edit config.h to change values ############################
*/

#include "can_rule_engine.h"
#include "driver/twai.h"
#include <ESP32-TWAI-CAN.hpp>
#include "config.h"
#include <cstdint>
#include <cstdio>
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define SPEED 1000
#define HAS_DISPLAY 1

#if (HAS_DISPLAY)
#include "ui.h"
#include "ui_code.hpp"
#include <lvgl.h>
#define CAN_TX 44
#define CAN_RX 43
#define LED_PIN 38
#else
#define CAN_TX 5
#define CAN_RX 4
#define LED_PIN 6
#endif

#define NUM_LEDS 20
#define FLICKER_INTERVAL 20

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
double observed_rpm_min = 0;
double observed_rpm_max = RPM_MAX * 1.2;
unsigned long last_flash_time = 0;
bool flash_state = true;

using u8 = uint8_t;
using u16 = uint16_t;

void handle_speed(const CanFrame &rxFrame);
void handle_rpm(const CanFrame &rxFrame);
void handle_engine_voltage(const CanFrame &rxFrame);
void handle_oil_pressure(const CanFrame &rxFrame);
void handle_oil_temp(const CanFrame &rxFrame);
void handle_gear_selection(const CanFrame &rxFrame);
void handle_engine_light(const CanFrame &rxFrame);
void updateRPMLEDs(double rpm);
void display_update();

class CompareIdentifier {
  u16 identifier;
public:
  CompareIdentifier(u16 identifier) : identifier(identifier) {};
  bool operator()(const CanFrame &rxFrame) const {
    return rxFrame.identifier == identifier;
  }
};

char buf[32];
CanFrame rxFrame;
RuleEngine<CanFrame> rule_engine;

bool update = false;
bool engine_error = false;
bool rpm_up = false;
bool rpm_down = false;
bool temperature = false;
bool pressure = false;
bool voltage = false;
bool gear = false;
double rpm_value = 0;

inline void toggle_min_threshold(double value, double min_value, bool &condition) {
  condition = (value < min_value);
}
inline void toggle_max_threshold(double value, double max_value, bool &condition) {
  condition = (value > max_value);
}
inline void toggle_out_of_range(double value, double min_value, double max_value, bool &condition) {
  condition = (value < min_value) || (value > max_value);
}

#if (HAS_DISPLAY)
inline bool is_visible(lv_obj_t *o) {
  return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void toggle_visibility(bool condition, lv_obj_t *ui_element) {
  bool visible = is_visible(ui_element);
  if (condition && !visible) {
    lv_obj_clear_flag(ui_element, LV_OBJ_FLAG_HIDDEN);
    update = true;
  } else if (!condition && visible) {
    lv_obj_add_flag(ui_element, LV_OBJ_FLAG_HIDDEN);
    update = true;
  }
}

void dim_text(bool condition, lv_obj_t *ui_element) {
  if (condition)
    lv_obj_set_style_text_color(ui_element, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  else
    lv_obj_set_style_text_color(ui_element, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void update_text_u16(u16 value, lv_obj_t *ui_element) {
  lv_snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  lv_label_set_text(ui_element, buf);
  update = true;
}

void update_text_float(double value, lv_obj_t *ui_element, int precision = 1) {
  char fmt[8];
  snprintf(fmt, sizeof(fmt), "%%.%df", precision);
  lv_snprintf(buf, sizeof(buf), fmt, value);
  lv_label_set_text(ui_element, buf);
  update = true;
}

void display_update_rpm() {
  toggle_visibility(!rpm_up, ui_erpmbackswitchup);
  toggle_visibility(!rpm_down, ui_erpmbackswitchdown);
}
void display_update_voltage() {
  toggle_visibility(voltage, ui_evoltageback);
}
void display_update_pressure() {
  toggle_visibility(pressure, ui_eoilpressureback);
}
void display_update_temperature() {
  toggle_visibility(temperature, ui_eoiltemperatureback);
}
void display_update_engine_error() {
  toggle_visibility(engine_error, ui_eengineback);
}
void display_update() {
  toggle_visibility(!rpm_up, ui_erpmbackswitchup);
  toggle_visibility(!rpm_down, ui_erpmbackswitchdown);
  toggle_visibility(engine_error, ui_eengineback);
  toggle_visibility(temperature, ui_eoiltemperatureback);
  toggle_visibility(pressure, ui_eoilpressureback);
  toggle_visibility(voltage, ui_evoltageback);
}
#endif 

void updateRPMLEDs(double rpm) {
  unsigned long now = millis();
  if (observed_rpm_max <= observed_rpm_min + 1) return;

  int leds_to_light = map(
      constrain(rpm, observed_rpm_min, observed_rpm_max),
      observed_rpm_min, observed_rpm_max, 0, NUM_LEDS);

  int green_zone_end = NUM_LEDS * 0.25;
  int yellow_zone_end = NUM_LEDS * 0.50;
  int red_zone_end = map(RPM_MAX, observed_rpm_min, observed_rpm_max, 0, NUM_LEDS);
  red_zone_end = constrain(red_zone_end, yellow_zone_end, NUM_LEDS);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < leds_to_light) {
      if (i < green_zone_end)
        strip.setPixelColor(i, strip.Color(0, 255, 0));
      else if (i < yellow_zone_end)
        strip.setPixelColor(i, strip.Color(255, 255, 0));
      else if (i < red_zone_end)
        strip.setPixelColor(i, strip.Color(255, 0, 0));
      else
        strip.setPixelColor(i, strip.Color(180, 0, 255));
    } else {
      strip.setPixelColor(i, 0, 0, 0);
    }
  }

  if (rpm > RPM_MAX) {
    if (now - last_flash_time > FLICKER_INTERVAL) {
      flash_state = !flash_state;
      last_flash_time = now;
    }
    if (!flash_state) {
      for (int i = 0; i < NUM_LEDS; i++)
        strip.setPixelColor(i, 0, 0, 0);
    }
  }

  strip.show();
}

void setup() {
  Serial.begin(9600);
  bool success = ESP32Can.begin(ESP32Can.convertSpeed(SPEED), CAN_TX, CAN_RX, 10, 10);

  if (success)
    Serial.println("CAN bus started!");
  else
    Serial.println("CAN bus failed!");

  strip.begin();
  strip.show();
  strip.setBrightness(20);

  rule_engine.add_rule(CompareIdentifier(0x370), &handle_speed);
  rule_engine.add_rule(CompareIdentifier(0x360), &handle_rpm);
  rule_engine.add_rule(CompareIdentifier(0x372), &handle_engine_voltage);
  rule_engine.add_rule(CompareIdentifier(0x361), &handle_oil_pressure);
  rule_engine.add_rule(CompareIdentifier(0x3E0), &handle_oil_temp);
  rule_engine.add_rule(CompareIdentifier(0x470), &handle_gear_selection);
  rule_engine.add_rule(CompareIdentifier(0x3E4), &handle_engine_light);

#if (HAS_DISPLAY)
  init_screen();
#else
  Serial.println("Headless mode (no display)");
#endif
}

void loop() {
  update = false;
  if (ESP32Can.readFrame(rxFrame, 1000)) {
    rule_engine.run(rxFrame);
  }

#if (HAS_DISPLAY)
  if (update)
    lv_timer_handler();
#endif

  updateRPMLEDs(rpm_value);
}

void handle_speed(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | rxFrame.data[1];
  u16 speed_val = raw / 10;
#if (HAS_DISPLAY)
  update_text_u16(speed_val, ui_espeed);
  lv_arc_set_value(ui_espeedarc, speed_val);
#else
  Serial.printf("Speed: %u km/h\n", speed_val);
#endif
}

void handle_rpm(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | rxFrame.data[1];
  rpm_value = raw;
#if (HAS_DISPLAY)
  update_text_u16((u16)raw, ui_erpm);
  lv_bar_set_value(ui_erpmbar, raw, LV_ANIM_OFF);
#endif
  toggle_max_threshold(rpm_value, RPM_MAX, rpm_up);
  toggle_min_threshold(rpm_value, RPM_MIN, rpm_down);
#if (HAS_DISPLAY)
  display_update_rpm();
#endif
  updateRPMLEDs(rpm_value);
}

void handle_engine_voltage(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | rxFrame.data[1];
  double val = raw / 10.0;
#if (HAS_DISPLAY)
  update_text_float(val, ui_evoltage, 1);
  toggle_min_threshold(val, VOLTAGE_MIN, voltage);
  dim_text(voltage, ui_evoltage);
  dim_text(voltage, ui_voltagedu);
  display_update_voltage();
#else
  Serial.printf("Voltage: %.1fV\n", val);
#endif
}

void handle_oil_pressure(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[2] << 8) | rxFrame.data[3];
  double val = raw / 10.0 - 101.3;
#if (HAS_DISPLAY)
  update_text_float(val, ui_eoilpressure, 1);
  toggle_min_threshold(val, PRESSURE_MIN, pressure);
  dim_text(pressure, ui_eoilpressure);
  dim_text(pressure, ui_oilpressuredu);
  display_update_pressure();
#else
  Serial.printf("Oil Pressure: %.1fkPa\n", val);
#endif
}

void handle_oil_temp(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[6] << 8) | rxFrame.data[7];
  double val = raw / 10.0 - 273.15;
#if (HAS_DISPLAY)
  update_text_float(val, ui_eoiltemperature, 1);
  toggle_max_threshold(val, TEMP_MAX, temperature);
  dim_text(temperature, ui_eoiltemperature);
  dim_text(temperature, ui_oiltemperaturedu);
  display_update_temperature();
#else
  Serial.printf("Oil Temp: %.1f°C\n", val);
#endif
}

void handle_gear_selection(const CanFrame &rxFrame) {
  u8 gear_val = rxFrame.data[7];
#if (HAS_DISPLAY)
  update_text_u16(gear_val, ui_egear);
#else
  Serial.printf("Gear: %u\n", gear_val);
#endif
}

void handle_engine_light(const CanFrame &rxFrame) {
  u8 val = (rxFrame.data[7] >> 7) & 0x01;
  engine_error = val != 0;
#if (HAS_DISPLAY)
  dim_text(engine_error, ui_enginedu);
  display_update_engine_error();
#else
  Serial.printf("Check Engine: %u\n", val);
#endif
}
