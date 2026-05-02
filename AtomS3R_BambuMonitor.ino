/*
  AtomS3R Bambu Lab Printer Monitor (LAN MQTT)
  ----------------------------------------------
  Target  : M5Stack AtomS3R (ESP32-S3-PICO-1-N8R8 / 128x128 IPS)
  IDE     : Arduino IDE 2.x  (Board: "ESP32S3 Dev Module" or "M5Stack-AtomS3")
  Libs    : M5Unified, PubSubClient, ArduinoJson, WiFi, WiFiClientSecure

  Display : Nozzle Temp / Bed Temp / Layer (cur/total) / Progress Bar / "Bambu" mark
  Source  : Bambu local MQTT broker  mqtts://<PRINTER_IP>:8883
            user: bblp / pass: <Access Code>  (Settings -> LAN Only)
            Topic: device/<SERIAL>/report

  License : MIT
  Repo    : https://github.com/<USER>/AtomS3R-BambuMonitor
*/

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"   // <-- credentials are kept out of git

// MQTT
const uint16_t MQTT_PORT = 8883;
char  MQTT_TOPIC_REPORT[96];
char  MQTT_TOPIC_REQUEST[96];

WiFiClientSecure netClient;
PubSubClient     mqtt(netClient);

// Cached status (P1/A1 push only deltas)
struct Status {
  float nozzle = 0.0f, nozzleTgt = 0.0f;
  float bed    = 0.0f, bedTgt    = 0.0f;
  int   layer  = 0,    totalLayer= 0;
  int   percent= 0;
  String gstate = "IDLE";
  uint32_t lastUpdate = 0;
} st;

bool needsRedraw = true;

// ---------- Display layout (128 x 128) -------------------------------
static void drawHeader() {
  M5.Display.fillRect(0, 0, 128, 11, TFT_BLACK);
  M5.Display.fillRect(2, 2, 7, 7, 0x07E0);     // green dot
  M5.Display.setTextDatum(top_left);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(12, 2);
  M5.Display.print("Bambu Monitor");
  M5.Display.drawFastHLine(0, 11, 128, 0x39C7);
}

static void drawTemps() {
  M5.Display.fillRect(0, 12, 128, 32, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(0xFD20, TFT_BLACK);  // orange
  M5.Display.setCursor(2, 14);
  M5.Display.print("Nozzle");
  M5.Display.setTextColor(0x07FF, TFT_BLACK);  // cyan
  M5.Display.setCursor(2, 30);
  M5.Display.print("Bed   ");

  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  char buf[24];
  snprintf(buf, sizeof(buf), "%3d/%3d", (int)st.nozzle, (int)st.nozzleTgt);
  M5.Display.setCursor(46, 12);
  M5.Display.print(buf);

  snprintf(buf, sizeof(buf), "%3d/%3d", (int)st.bed, (int)st.bedTgt);
  M5.Display.setCursor(46, 28);
  M5.Display.print(buf);
}

static void drawLayer() {
  M5.Display.fillRect(0, 46, 128, 32, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(0xFFE0, TFT_BLACK); // yellow
  M5.Display.setCursor(2, 48);
  M5.Display.print("Layer");

  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d/%d", st.layer, st.totalLayer);
  M5.Display.setCursor(2, 58);
  M5.Display.print(buf);
}

static void drawProgress() {
  const int x = 4, y = 82, w = 120, h = 14;
  M5.Display.drawRect(x, y, w, h, TFT_WHITE);
  M5.Display.fillRect(x + 1, y + 1, w - 2, h - 2, 0x18C3);
  int fillW = (w - 2) * st.percent / 100;
  if (fillW > 0) {
    M5.Display.fillRect(x + 1, y + 1, fillW, h - 2, 0x07E0);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", st.percent);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(buf, 64, y + h / 2);
  M5.Display.setTextDatum(top_left);
}

static void drawFooter(const char* msg) {
  M5.Display.fillRect(0, 100, 128, 28, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(0x8410, TFT_BLACK);
  M5.Display.setCursor(2, 104);
  M5.Display.print("State:");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 116);
  M5.Display.print(msg);
}

static void redrawAll() {
  drawHeader();
  drawTemps();
  drawLayer();
  drawProgress();
  drawFooter(st.gstate.c_str());
}

// ---------- MQTT message parsing -------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("JSON err: %s\n", err.c_str());
    return;
  }

  JsonObject p = doc["print"].as<JsonObject>();
  if (p.isNull()) return;

  bool changed = false;

  if (p["nozzle_temper"].is<float>())        { st.nozzle    = p["nozzle_temper"];        changed = true; }
  if (p["nozzle_target_temper"].is<float>()) { st.nozzleTgt = p["nozzle_target_temper"]; changed = true; }
  if (p["bed_temper"].is<float>())           { st.bed       = p["bed_temper"];           changed = true; }
  if (p["bed_target_temper"].is<float>())    { st.bedTgt    = p["bed_target_temper"];    changed = true; }
  if (p["layer_num"].is<int>())              { st.layer     = p["layer_num"];            changed = true; }
  if (p["total_layer_num"].is<int>())        { st.totalLayer= p["total_layer_num"];      changed = true; }
  if (p["mc_percent"].is<int>())             { st.percent   = p["mc_percent"];           changed = true; }
  if (p["gcode_state"].is<const char*>())    { st.gstate    = (const char*)p["gcode_state"]; changed = true; }

  if (changed) {
    st.lastUpdate = millis();
    needsRedraw = true;
  }
}

static void requestPushAll() {
  StaticJsonDocument<128> req;
  JsonObject pushing = req["pushing"].to<JsonObject>();
  pushing["sequence_id"] = "0";
  pushing["command"]     = "pushall";
  char out[160];
  size_t n = serializeJson(req, out, sizeof(out));
  mqtt.publish(MQTT_TOPIC_REQUEST, (uint8_t*)out, n);
}

static bool mqttConnect() {
  Serial.print("MQTT connect...");
  netClient.setInsecure();
  netClient.setTimeout(15);

  mqtt.setServer(PRINTER_IP, MQTT_PORT);
  mqtt.setBufferSize(40 * 1024);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(onMqttMessage);

  String clientId = "AtomS3R-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str(), "bblp", ACCESS_CODE)) {
    Serial.println("OK");
    mqtt.subscribe(MQTT_TOPIC_REPORT, 0);
    delay(200);
    requestPushAll();
    return true;
  }
  Serial.printf("fail rc=%d\n", mqtt.state());
  return false;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(160);
  M5.Display.fillScreen(TFT_BLACK);

  Serial.begin(115200);
  delay(100);

  snprintf(MQTT_TOPIC_REPORT,  sizeof(MQTT_TOPIC_REPORT),  "device/%s/report",  PRINTER_SERIAL);
  snprintf(MQTT_TOPIC_REQUEST, sizeof(MQTT_TOPIC_REQUEST), "device/%s/request", PRINTER_SERIAL);

  drawHeader();
  drawFooter("WiFi connecting");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    drawFooter("WiFi FAIL");
    return;
  }
  drawFooter("MQTT connecting");
}

void loop() {
  M5.update();

  if (WiFi.status() != WL_CONNECTED) {
    delay(500);
    return;
  }

  if (!mqtt.connected()) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 3000) {
      lastTry = millis();
      drawFooter("MQTT connecting");
      if (mqttConnect()) {
        needsRedraw = true;
      } else {
        drawFooter("MQTT retry...");
      }
    }
  } else {
    mqtt.loop();
  }

  if (needsRedraw) {
    needsRedraw = false;
    redrawAll();
  }

  if (st.lastUpdate && millis() - st.lastUpdate > 60000) {
    drawFooter("No data >60s");
    st.lastUpdate = 0;
  }

  if (M5.BtnA.wasClicked() && mqtt.connected()) {
    requestPushAll();
  }

  delay(10);
}
