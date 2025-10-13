/*
########################## open include directory and edit config.h to change values ############################
*/
#include "can_rule_engine.h"
#include "driver/twai.h"
#include "ui.h"
#include "ui_code.hpp"
#include <ESP32-TWAI-CAN.hpp>
#include <lvgl.h>
#include "config.h"
#include <cstdint>
#include <cstdio>

//if we're using Temp and Pressure values i feel it would be better using these standard fixed width integer libraries (cstdint and cstdio)

using u8 = uint8_t;      //converted to modern sytax w using
using u16 = uint16_t;

#define CAN_TX 44
#define CAN_RX 43
//#define CAN_TX 5
//#define CAN_RX 4
#define SPEED 1000
#define HAS_DISPLAY 1
// #define P 43
// works   38 44 43
// doesn't 19

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
  bool operator()(const CanFrame &rxFrame) const {   //const as doesnt modify object 
    return rxFrame.identifier == identifier;
  }
};

//larger buffer of 32 cuz why not
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

inline void toggle_min_threshold(double value, double min_value, bool &condition) {
  condition = (value < min_value);
}
inline void toggle_max_threshold(double value, double max_value, bool &condition) {
  condition = (value > max_value);
}

inline void toggle_out_of_range(double value, double min_value, double max_value, bool &condition) {
  condition = (value < min_value) || (value > max_value);
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
  std::snprintf(fmt, sizeof(fmt), "%%.%df", precision); // e.g. "%.1f"
  lv_snprintf(buf, sizeof(buf), fmt, value);
  lv_label_set_text(ui_element, buf);
  update = true;
}

void setup()
{
  Serial.begin(115200);

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
#endif
}

void loop() {
  update = false;
  if (ESP32Can.readFrame(rxFrame, 1000)) {
    rule_engine.run(rxFrame);
  }

  if (update) {
    lv_timer_handler();
  }
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


//cut this down to boolean assignment instead of branching w ifs and else, and also used double so checks work for floats. (temps and shizzle)
//i wanna use two update text functions, one for u16 and one for floats to make life easier and keep decimal places correct for 

//again removed redundant branching and also only changes flags and sets update if visibility would change

/*
Voltage works
Temperature Works Change to Celcius
Pressure Works kPa
RPM Works
Gear Works but is slow?

Check Engine Light Doesn't Work: issue, it's always on

Speed Don't Know
*/


void display_update() {
  toggle_visibility(!rpm_up, ui_erpmbackswitchup);
  toggle_visibility(!rpm_down, ui_erpmbackswitchdown);

  // other indicators remain direct
  toggle_visibility(engine_error, ui_eengineback);
  toggle_visibility(temperature, ui_eoiltemperatureback);
  toggle_visibility(pressure, ui_eoilpressureback);
  toggle_visibility(voltage, ui_evoltageback);
}

void handle_speed(const CanFrame &rxFrame) {
    // ui_espeed          Speed 0x370 0-1 vehicle speed km/h y = x/10
  u16 bit0 = (u16)rxFrame.data[0];
  u16 bit1 = (u16)rxFrame.data[1];
  u16 raw = ((bit0 << 8) | bit1);
  u16 speed_val = raw / 10; // integer km/h as before
  update_text_u16(speed_val, ui_espeed);
  lv_arc_set_value(ui_espeedarc, speed_val);
#if (HAS_DISPLAY)
// Update display
// lv_timer_handler();
// delay(10);
#else
  lv_snprintf(buf, sizeof(buf), "%u", (unsigned)speed_val);
  Serial.print("Speed: ");
  Serial.println(buf);
#endif
}

void handle_rpm(const CanFrame &rxFrame) {
  u16 bit0 = (u16)rxFrame.data[0];
  u16 bit1 = (u16)rxFrame.data[1];
  u16 raw = ((bit0 << 8) | bit1);
  double rpm_val = (double)raw;

  update_text_u16((u16)raw, ui_erpm);
  lv_bar_set_value(ui_erpmbar, raw, LV_ANIM_OFF);

  toggle_max_threshold(rpm_val, (double)RPM_MAX, rpm_up);
  toggle_min_threshold(rpm_val, (double)RPM_MIN, rpm_down);

  display_update_rpm();  
}

void handle_engine_voltage(const CanFrame &rxFrame) {
  // ui_evoltage        V     0x372 0-1 battery voltage Volts y = x/10
  u16 bit0 = (u16)rxFrame.data[0];
  u16 bit1 = (u16)rxFrame.data[1];
  u16 raw = ((bit0 << 8) | bit1);
  double voltage_val = raw / 10.0; // keep one decimal
  update_text_float(voltage_val, ui_evoltage, 1);

  toggle_min_threshold(voltage_val, VOLTAGE_MIN, voltage);
  dim_text(voltage, ui_evoltage);
  dim_text(voltage, ui_voltagedu);
  display_update_voltage();
#if (HAS_DISPLAY)
#else
  lv_snprintf(buf, sizeof(buf), "%.1f", voltage_val);
  Serial.print("Voltage: ");
  Serial.println(buf);
#endif
}


void handle_oil_pressure(const CanFrame &rxFrame) {
    //ui_eoilpressure    P     0x361 2-3 oil pressure kPa y = x/10 - 101.3
  // ui_eoilpressure
  // ui_eoilpressureback
  // 0x361; bits 2-3 Oil Pressure; y = x/10 - 101.3
  u16 bit0 = (u16)rxFrame.data[2];
  u16 bit1 = (u16)rxFrame.data[3];
  u16 raw = ((bit0 << 8) | bit1);
  double pressure_val = raw / 10.0 - 101.3;
  update_text_float(pressure_val, ui_eoilpressure, 1);
  toggle_min_threshold(pressure_val, PRESSURE_MIN, pressure);
  dim_text(pressure, ui_eoilpressure);
  dim_text(pressure, ui_oilpressuredu);
  display_update_pressure();
#if (HAS_DISPLAY)
#else
  lv_snprintf(buf, sizeof(buf), "%.1f", pressure_val);
  Serial.print("Oil Pressure (kPa): ");
  Serial.println(buf);
#endif
}

void handle_oil_temp(const CanFrame &rxFrame) {
  //ui_eoiltemperature T     0x3E0 6-7 oil temp     K   y = x/10
  // ui_eoiltemperature
  // ui_eoiltemperatureback
  // 0x3E0; bits 6-7; Oil temperature; y = x/10
  u16 bit0 = (u16)rxFrame.data[6];
  u16 bit1 = (u16)rxFrame.data[7];
  u16 raw = ((bit0 << 8) | bit1);
  double temperature_val = raw / 10.0 - 273.15;
  update_text_float(temperature_val, ui_eoiltemperature, 1);

  toggle_max_threshold(temperature_val, TEMP_MAX, temperature);
  dim_text(temperature, ui_eoiltemperature);
  dim_text(temperature, ui_oiltemperaturedu);
  display_update_temperature();

#if (HAS_DISPLAY)
#else
  lv_snprintf(buf, sizeof(buf), "%.1f", temperature_val);
  Serial.print("Oil Temp (°C): ");
  Serial.println(buf);
#endif
}

void handle_gear_selection(const CanFrame &rxFrame) {
  //ui_egear           Gear  0x470 6 gear selector position enum
  //ui_egear
  // 0x470; bits 7; gear position; enum val ??
  u16 gear_val = (u16)rxFrame.data[7]; // 7 gear
  update_text_u16(gear_val, ui_egear);

#if (HAS_DISPLAY)
#else
  lv_snprintf(buf, sizeof(buf), "%u", (unsigned)gear_val);
  Serial.print("Gear: ");
  Serial.println(buf);
#endif
}




void handle_engine_light(const CanFrame &rxFrame) {
  // ui_eengine         E     0x3E4 7:7 check engine light boolean 0=off, 1=on
  u8 engine_light_val = (rxFrame.data[7] >> 7) & 0x01;
  engine_error = (engine_light_val != 0);
  dim_text(engine_error, ui_enginedu);
  display_update_engine_error();

#if (HAS_DISPLAY)
#else
  lv_snprintf(buf, sizeof(buf), "%u", (unsigned)engine_light_val);
  Serial.print("Check Engine: ");
  Serial.println(buf);
#endif
}

