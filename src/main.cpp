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
#include <WiFi.h> 

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

const char* AP_SSID = "ESP32_Dashboard";
const char* AP_PASS = "12345678";   // must be ≥8 chars

WiFiServer wifiServer(23);
WiFiClient wifiClient;

void wifiSerialPrint(const String &msg) {
  if (wifiClient && wifiClient.connected()) wifiClient.print(msg);
}
void wifiSerialPrintln(const String &msg) { wifiSerialPrint(msg + "\r\n"); }

void setupWiFiSerial() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  wifiServer.begin();

#if (HAS_DISPLAY)
  lv_obj_t* ip_label = lv_label_create(lv_scr_act());
  char buf[64];
  snprintf(buf, sizeof(buf), "Wi-Fi: %s\nIP: %s", AP_SSID, myIP.toString().c_str());
  lv_label_set_text(ip_label, buf);
  lv_obj_align(ip_label, LV_ALIGN_BOTTOM_MID, 0, -10);
#endif

  wifiSerialPrintln("ESP32 Wi-Fi Serial Ready");
  wifiSerialPrintln("Connect to Wi-Fi:");
  wifiSerialPrintln(String("SSID: ") + AP_SSID);
  wifiSerialPrintln(String("PASS: ") + AP_PASS);
  wifiSerialPrintln("Telnet IP: 192.168.4.1, Port: 23");
}

void handleWiFiSerial() {
  if (!wifiClient || !wifiClient.connected()) {
    wifiClient = wifiServer.available();
    if (wifiClient)
      wifiClient.println("Connected to ESP32 Wi-Fi Serial Monitor (AP mode, 192.168.4.1)");
  }
}

#define SerialOut(x)    { wifiSerialPrint(x); }
#define SerialOutln(x)  { wifiSerialPrint(String(x) + "\r\n"); }
#define SerialOutf(...) { char b[256]; snprintf(b,sizeof(b),__VA_ARGS__); wifiSerialPrint(b); wifiSerialPrint("\r\n"); }


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
  bool operator()(const CanFrame &rxFrame) const { return rxFrame.identifier == identifier; }
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

inline void toggle_min_threshold(double v,double min,bool &c){ c=(v<min);}
inline void toggle_max_threshold(double v,double max,bool &c){ c=(v>max);}
inline void toggle_out_of_range(double v,double min,double max,bool &c){ c=(v<min)||(v>max);}

#if (HAS_DISPLAY)
inline bool is_visible(lv_obj_t *o){ return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);}
void toggle_visibility(bool cond, lv_obj_t *ui){
  bool vis=is_visible(ui);
  if(cond&&!vis){ lv_obj_clear_flag(ui,LV_OBJ_FLAG_HIDDEN); update=true;}
  else if(!cond&&vis){ lv_obj_add_flag(ui,LV_OBJ_FLAG_HIDDEN); update=true;}
}
void dim_text(bool cond, lv_obj_t *ui){
  lv_obj_set_style_text_color(ui, lv_color_hex(cond?0xFFFFFF:0x555555), LV_PART_MAIN|LV_STATE_DEFAULT);
}
void update_text_u16(u16 v, lv_obj_t *ui){
  lv_snprintf(buf,sizeof(buf),"%u",(unsigned)v);
  lv_label_set_text(ui,buf);
  update=true;
}
void update_text_float(double v, lv_obj_t *ui,int p=1){
  char fmt[8]; snprintf(fmt,sizeof(fmt),"%%.%df",p);
  lv_snprintf(buf,sizeof(buf),fmt,v);
  lv_label_set_text(ui,buf);
  update=true;
}
void display_update_rpm(){ toggle_visibility(!rpm_up,ui_erpmbackswitchup); toggle_visibility(!rpm_down,ui_erpmbackswitchdown);}
void display_update_voltage(){ toggle_visibility(voltage,ui_evoltageback);}
void display_update_pressure(){ toggle_visibility(pressure,ui_eoilpressureback);}
void display_update_temperature(){ toggle_visibility(temperature,ui_eoiltemperatureback);}
void display_update_engine_error(){ toggle_visibility(engine_error,ui_eengineback);}
void display_update(){
  toggle_visibility(!rpm_up,ui_erpmbackswitchup);
  toggle_visibility(!rpm_down,ui_erpmbackswitchdown);
  toggle_visibility(engine_error,ui_eengineback);
  toggle_visibility(temperature,ui_eoiltemperatureback);
  toggle_visibility(pressure,ui_eoilpressureback);
  toggle_visibility(voltage,ui_evoltageback);
}
#endif

void updateRPMLEDs(double rpm){
  unsigned long now=millis();
  if(observed_rpm_max<=observed_rpm_min+1) return;
  int leds=map(constrain(rpm,observed_rpm_min,observed_rpm_max),
               observed_rpm_min,observed_rpm_max,0,NUM_LEDS);
  int g_end=NUM_LEDS*0.25, y_end=NUM_LEDS*0.50;
  int r_end=map(RPM_MAX,observed_rpm_min,observed_rpm_max,0,NUM_LEDS);
  r_end=constrain(r_end,y_end,NUM_LEDS);
  for(int i=0;i<NUM_LEDS;i++){
    if(i<leds){
      if(i<g_end) strip.setPixelColor(i,strip.Color(0,255,0));
      else if(i<y_end) strip.setPixelColor(i,strip.Color(255,255,0));
      else if(i<r_end) strip.setPixelColor(i,strip.Color(255,0,0));
      else strip.setPixelColor(i,strip.Color(180,0,255));
    } else strip.setPixelColor(i,0,0,0);
  }
  if(rpm>RPM_MAX){
    if(now-last_flash_time>FLICKER_INTERVAL){ flash_state=!flash_state; last_flash_time=now;}
    if(!flash_state) for(int i=0;i<NUM_LEDS;i++) strip.setPixelColor(i,0,0,0);
  }
  strip.show();
}

void setup() {
#if (HAS_DISPLAY)
  init_screen();  
#endif
  setupWiFiSerial();  // always IP 192.168.4.1

  bool success = ESP32Can.begin(ESP32Can.convertSpeed(SPEED), CAN_TX, CAN_RX, 10, 10);
  if (success) {
    SerialOutln("CAN bus started!");
  } else {
    SerialOutln("CAN bus failed!");
  }

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
}

void loop(){
  handleWiFiSerial();
  update=false;

  if(ESP32Can.readFrame(rxFrame,1000)) rule_engine.run(rxFrame);

#if (HAS_DISPLAY)
  if(update) lv_timer_handler();
#endif
  updateRPMLEDs(rpm_value);
}

void handle_speed(const CanFrame &rx){
  u16 raw=((u16)rx.data[0]<<8)|rx.data[1];
  u16 speed=raw/10;
  SerialOutf("Speed: %u km/h\n",speed);
#if (HAS_DISPLAY)
  update_text_u16(speed,ui_espeed);
  lv_arc_set_value(ui_espeedarc,speed);
#endif
}

void handle_rpm(const CanFrame &rx){
  u16 raw=((u16)rx.data[0]<<8)|rx.data[1];
  rpm_value=raw;
  SerialOutf("RPM: %.0f\n", rpm_value);
#if (HAS_DISPLAY)
  update_text_u16(raw,ui_erpm);
  lv_bar_set_value(ui_erpmbar,raw,LV_ANIM_OFF);
#endif
  toggle_max_threshold(rpm_value,RPM_MAX,rpm_up);
  toggle_min_threshold(rpm_value,RPM_MIN,rpm_down);
#if (HAS_DISPLAY)
  display_update_rpm();
#endif
  updateRPMLEDs(rpm_value);
}

void handle_engine_voltage(const CanFrame &rx){
  u16 raw=((u16)rx.data[0]<<8)|rx.data[1];
  double val=raw/10.0;
  SerialOutf("Voltage: %.1fV\n",val);
#if (HAS_DISPLAY)
  update_text_float(val,ui_evoltage,1);
  toggle_min_threshold(val,VOLTAGE_MIN,voltage);
  dim_text(voltage,ui_evoltage);
  dim_text(voltage,ui_voltagedu);
  display_update_voltage();
#endif
}

void handle_oil_pressure(const CanFrame &rx){
  u16 raw=((u16)rx.data[2]<<8)|rx.data[3];
  double val=raw/10.0-101.3;
  SerialOutf("Oil Pressure: %.1fkPa\n",val);
#if (HAS_DISPLAY)
  update_text_float(val,ui_eoilpressure,1);
  toggle_min_threshold(val,PRESSURE_MIN,pressure);
  dim_text(pressure,ui_eoilpressure);
  dim_text(pressure,ui_oilpressuredu);
  display_update_pressure();
#endif
}

void handle_oil_temp(const CanFrame &rx){
  u16 raw=((u16)rx.data[6]<<8)|rx.data[7];
  double val=raw/10.0-273.15;
  SerialOutf("Oil Temp: %.1f°C\n",val);
#if (HAS_DISPLAY)
  update_text_float(val,ui_eoiltemperature,1);
  toggle_max_threshold(val,TEMP_MAX,temperature);
  dim_text(temperature,ui_eoiltemperature);
  dim_text(temperature,ui_oiltemperaturedu);
  display_update_temperature();
#endif
}

void handle_gear_selection(const CanFrame &rx){
  u8 gear=rx.data[7];
  SerialOutf("Gear: %u\n",gear);
#if (HAS_DISPLAY)
  update_text_u16(gear,ui_egear);
#endif
}

void handle_engine_light(const CanFrame &rx){
  u8 val=(rx.data[7]>>7)&0x01;
  engine_error=val!=0;
  SerialOutf("Check Engine: %u\n",val);
#if (HAS_DISPLAY)
  dim_text(engine_error,ui_enginedu);
  display_update_engine_error();
#endif
}
