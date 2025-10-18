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
#define LED_PIN  38          
#else
#define CAN_TX 5
#define CAN_RX 4
#define LED_PIN  6        
#endif

#define NUM_LEDS 20          
#define FLICKER_INTERVAL 20 

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

double observed_rpm_min = 0;
double observed_rpm_max = RPM_MAX * 1.2; 
unsigned long last_flash_time = 0;
bool flash_state = true;
int last_leds_to_light = 0;

using u8 = uint8_t;
using u16 = uint16_t;

void canbusTask(void *pvParameters);
void displayTask(void *pvParameters);
void handle_speed(const CanFrame &rxFrame);
void handle_rpm(const CanFrame &rxFrame);
void handle_engine_voltage(const CanFrame &rxFrame);
void handle_oil_pressure(const CanFrame &rxFrame);
void handle_oil_temp(const CanFrame &rxFrame);
void handle_gear_selection(const CanFrame &rxFrame);
void handle_engine_light(const CanFrame &rxFrame);
void updateRPMLEDs(double rpm);

CanFrame rxFrame;
RuleEngine<CanFrame> rule_engine;

struct SharedData {
  double speed;
  double rpm;
  double voltage;
  double pressure;
  double temperature;
  uint8_t gear;
  bool engine_error;
  bool rpm_up;
  bool rpm_down;
  bool pressure_low;
  bool voltage_low;
  bool temperature_high;
} shared;

SemaphoreHandle_t dataMutex;
TaskHandle_t canbusTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

inline void toggle_min_threshold(double value, double min_value, bool &condition) {
  condition = (value < min_value);
}
inline void toggle_max_threshold(double value, double max_value, bool &condition) {
  condition = (value > max_value);
}

class CompareIdentifier {
  uint16_t identifier;
public:
  CompareIdentifier(uint16_t identifier) : identifier(identifier) {}
  bool operator()(const CanFrame &rxFrame) const { return rxFrame.identifier == identifier; }
};

void updateRPMLEDs(double rpm) {
  unsigned long now = millis();
  if (observed_rpm_max <= observed_rpm_min + 1) return;

  int leds_to_light = map(
      constrain(rpm, observed_rpm_min, observed_rpm_max),
      observed_rpm_min, observed_rpm_max, 0, NUM_LEDS);

  int green_zone_end  = NUM_LEDS * 0.25;
  int yellow_zone_end = NUM_LEDS * 0.50;
  int red_zone_end    = map(RPM_MAX, observed_rpm_min, observed_rpm_max, 0, NUM_LEDS);
  if (red_zone_end < yellow_zone_end) red_zone_end = yellow_zone_end;
  if (red_zone_end > NUM_LEDS) red_zone_end = NUM_LEDS;

  last_leds_to_light = leds_to_light;

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
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0, 0, 0);
    }
  }

  strip.show();
}

void setup() {
  Serial.begin(9600);
  Serial.println("Starting...");

  bool success = ESP32Can.begin(ESP32Can.convertSpeed(SPEED), CAN_TX, CAN_RX, 10, 10);
  if (success) Serial.println("CAN bus started!");
  else Serial.println("CAN bus failed!");

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

  dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(canbusTask, "CAN Task", 4096, NULL, 2, &canbusTaskHandle, 0);
  xTaskCreatePinnedToCore(displayTask, "Display Task", 8192, NULL, 1, &displayTaskHandle, 1);
}

void canbusTask(void *pvParameters) {
  for (;;) {
    if (ESP32Can.readFrame(rxFrame, 100)) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      rule_engine.run(rxFrame);
      xSemaphoreGive(dataMutex);
      if (displayTaskHandle != NULL) xTaskNotifyGive(displayTaskHandle);
    }
    vTaskDelay(1);
  }
}

void loop() {}

void displayTask(void *pvParameters) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreTake(dataMutex, portMAX_DELAY);

#if (HAS_DISPLAY)
    lv_label_set_text_fmt(ui_espeed, "%d", (int)shared.speed);
    lv_label_set_text_fmt(ui_erpm, "%d", (int)shared.rpm);
    lv_label_set_text_fmt(ui_egear, "%d", (int)shared.gear);
    lv_label_set_text_fmt(ui_evoltage, "%.1f", shared.voltage);
    lv_label_set_text_fmt(ui_eoilpressure, "%.1f", shared.pressure);
    lv_label_set_text_fmt(ui_eoiltemperature, "%.1f", shared.temperature);

    lv_arc_set_value(ui_espeedarc, shared.speed);
    lv_bar_set_value(ui_erpmbar, shared.rpm, LV_ANIM_OFF);

    if (shared.engine_error)
      lv_obj_clear_flag(ui_eengineback, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(ui_eengineback, LV_OBJ_FLAG_HIDDEN);

    lv_timer_handler();
#else
    Serial.printf("Speed: %.1f km/h | RPM: %.0f | Gear: %u | Voltage: %.1fV | Oil P: %.1f kPa | Oil T: %.1f°C | CheckEng: %d\n",
                  shared.speed, shared.rpm, shared.gear, shared.voltage,
                  shared.pressure, shared.temperature, shared.engine_error);
#endif

    updateRPMLEDs(shared.rpm);
    xSemaphoreGive(dataMutex);
  }
}

void handle_speed(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  shared.speed = raw / 10.0;
}
void handle_rpm(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  shared.rpm = raw;
  toggle_max_threshold(shared.rpm, (double)RPM_MAX, shared.rpm_up);
  toggle_min_threshold(shared.rpm, (double)RPM_MIN, shared.rpm_down);
}
void handle_engine_voltage(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[0] << 8) | (u16)rxFrame.data[1];
  shared.voltage = raw / 10.0;
  toggle_min_threshold(shared.voltage, VOLTAGE_MIN, shared.voltage_low);
}
void handle_oil_pressure(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[2] << 8) | (u16)rxFrame.data[3];
  shared.pressure = raw / 10.0 - 101.3;
  toggle_min_threshold(shared.pressure, PRESSURE_MIN, shared.pressure_low);
}
void handle_oil_temp(const CanFrame &rxFrame) {
  u16 raw = ((u16)rxFrame.data[6] << 8) | (u16)rxFrame.data[7];
  shared.temperature = raw / 10.0 - 273.15;
  toggle_max_threshold(shared.temperature, TEMP_MAX, shared.temperature_high);
}
void handle_gear_selection(const CanFrame &rxFrame) {
  shared.gear = rxFrame.data[7];
}
void handle_engine_light(const CanFrame &rxFrame) {
  shared.engine_error = ((rxFrame.data[7] >> 7) & 0x01);
}
