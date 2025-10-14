/*
########################## open include directory and edit config.h to change values ############################
*/
#include "can_rule_engine.h"
#include "driver/twai.h"
#include <ESP32-TWAI-CAN.hpp>
#include "config.h"
#include <cstdint>
#include <cstdio>

#define CAN_TX 5
#define CAN_RX 4
#define SPEED 1000
#define HAS_DISPLAY 0

#if (HAS_DISPLAY)
#include "ui.h"
#include "ui_code.hpp"
#include <lvgl.h>
#endif

using u8 = uint8_t;
using u16 = uint16_t;

void handle_speed(const CanFrame &rxFrame);
void handle_rpm(const CanFrame &rxFrame);
void handle_engine_voltage(const CanFrame &rxFrame);
void handle_oil_pressure(const CanFrame &rxFrame);
void handle_oil_temp(const CanFrame &rxFrame);
void handle_gear_selection(const CanFrame &rxFrame);
void handle_engine_light(const CanFrame &rxFrame);
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
  bool currently_visible = is_visible(ui_element);
  if (condition && !currently_visible) {
    lv_obj_clear_flag(ui_element, LV_OBJ_FLAG_HIDDEN);
    update = true;
  } else if (!condition && currently_visible) {
    lv_obj_add_flag(ui_element, LV_OBJ_FLAG_HIDDEN);
    update = true;
  }
}

void dim_text(bool condition, lv_obj_t *ui_element) {
  if (condition) {
    lv_obj_set_style_text_color(ui_element, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  } else {
    lv_obj_set_style_text_color(ui_element, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

void update_text_u16(u16 value, lv_obj_t *ui_element) {
  lv_snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  lv_label_set_text(ui_element, buf);
  update = true;
}

void update_text_float(double value, lv_obj_t *ui_element, int precision = 1) {
  char fmt[8];
  std::snprintf(fmt, sizeof(fmt), "%%.%df", precision);
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

#endif // HAS_DISPLAY


void setup() {
  Serial.begin(9600);

  auto SUCCESS =
      ESP32Can.begin(ESP32Can.convertSpeed(SPEED), CAN_TX, CAN_RX, 10, 10);

  if (SUCCESS) {
    Serial.println("CAN bus started!");
  } else {
    Serial.println("CAN bus failed!");
  }

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
  Serial.println("Running in headless mode (no display)");
#endif
}

void loop() {
  update = false;

  if (ESP32Can.readFrame(rxFrame, 1000)) {
    rule_engine.run(rxFrame);
  }

#if (HAS_DISPLAY)
  if (update) {
    lv_timer_handler();
  }
#endif
}


void handle_speed(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  u16 speed_val = raw / 10;

#if (HAS_DISPLAY)
  update_text_u16(speed_val, ui_espeed);
  lv_arc_set_value(ui_espeedarc, speed_val);
#else
  snprintf(buf, sizeof(buf), "%u", (unsigned)speed_val);
  Serial.print("Speed: ");
  Serial.println(buf);
#endif
}

void handle_rpm(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  double rpm_val = (double)raw;

#if (HAS_DISPLAY)
  update_text_u16((u16)raw, ui_erpm);
  lv_bar_set_value(ui_erpmbar, raw, LV_ANIM_OFF);
  toggle_max_threshold(rpm_val, (double)RPM_MAX, rpm_up);
  toggle_min_threshold(rpm_val, (double)RPM_MIN, rpm_down);
  display_update_rpm();
#else
  Serial.printf("RPM: %.0f\n", rpm_val);
#endif
}

void handle_engine_voltage(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  double voltage_val = raw / 10.0;

#if (HAS_DISPLAY)
  update_text_float(voltage_val, ui_evoltage, 1);
  toggle_min_threshold(voltage_val, VOLTAGE_MIN, voltage);
  dim_text(voltage, ui_evoltage);
  dim_text(voltage, ui_voltagedu);
  display_update_voltage();
#else
  snprintf(buf, sizeof(buf), "%.1f", voltage_val);
  Serial.print("Voltage: ");
  Serial.println(buf);
#endif
}

void handle_oil_pressure(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[2] << 8) | (u16)rxFrame.data[3];
  double pressure_val = raw / 10.0 - 101.3;

#if (HAS_DISPLAY)
  update_text_float(pressure_val, ui_eoilpressure, 1);
  toggle_min_threshold(pressure_val, PRESSURE_MIN, pressure);
  dim_text(pressure, ui_eoilpressure);
  dim_text(pressure, ui_oilpressuredu);
  display_update_pressure();
#else
  snprintf(buf, sizeof(buf), "%.1f", pressure_val);
  Serial.print("Oil Pressure (kPa): ");
  Serial.println(buf);
#endif
}

void handle_oil_temp(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[6] << 8) | (u16)rxFrame.data[7];
  double temperature_val = raw / 10.0 - 273.15;

#if (HAS_DISPLAY)
  update_text_float(temperature_val, ui_eoiltemperature, 1);
  toggle_max_threshold(temperature_val, TEMP_MAX, temperature);
  dim_text(temperature, ui_eoiltemperature);
  dim_text(temperature, ui_oiltemperaturedu);
  display_update_temperature();
#else
  snprintf(buf, sizeof(buf), "%.1f", temperature_val);
  Serial.print("Oil Temp (°C): ");
  Serial.println(buf);
#endif
}

void handle_gear_selection(const CanFrame &rxFrame) {
  u16 gear_val = (u16)rxFrame.data[7];

#if (HAS_DISPLAY)
  update_text_u16(gear_val, ui_egear);
#else
  snprintf(buf, sizeof(buf), "%u", (unsigned)gear_val);
  Serial.print("Gear: ");
  Serial.println(buf);
#endif
}

void handle_engine_light(const CanFrame &rxFrame) {
  u8 engine_light_val = (rxFrame.data[7] >> 7) & 0x01;
  engine_error = (engine_light_val != 0);

#if (HAS_DISPLAY)
  dim_text(engine_error, ui_enginedu);
  display_update_engine_error();
#else
  snprintf(buf, sizeof(buf), "%u", (unsigned)engine_light_val);
  Serial.print("Check Engine: ");
  Serial.println(buf);
#endif
}