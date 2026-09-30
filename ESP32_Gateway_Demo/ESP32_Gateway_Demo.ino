#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>           // NTP-based Unix timestamp

// =============================================================================
// DEMO FILE — Uses hardcoded sensor values. No physical sensors required.
// Publishes to the same MQTT broker and topic as ESP32_Gateway.ino so the
// Raspberry Pi pipeline can be tested end-to-end without hardware.
// =============================================================================

// --- WiFi & MQTT Configuration (mirrors ESP32_Gateway.ino) ---
const char* ssid        = "Pixel_1811";
const char* password    = "bikramhaz";
const char* mqtt_server = "10.184.234.236";   // Raspberry Pi IP
const int   mqtt_port   = 1883;

// --- NTP Configuration ---
const char* ntp_server  = "pool.ntp.org";
const long  gmt_offset  = 19800;   // IST = UTC+5:30 = 5*3600+30*60
const int   dst_offset  = 0;

WiFiClient   espClient;
PubSubClient client(espClient);

unsigned long lastMsgTime = 0;
const long    interval    = 5000;   // Publish every 5 seconds

// ---------------------------------------------------------------------------
// Hardcoded demo readings — edit these to simulate different patients / cases
// ---------------------------------------------------------------------------
const char*  DEVICE_ID        = "patient_01";
const int    HEART_RATE       = 78;     // bpm      — MAX30102
const int    SPO2             = 98;     // %         — MAX30102
const float  BODY_TEMPERATURE = 37.2;  // °C        — MLX90614 (IR)
const int    BP_SYS           = 122;   // mmHg sys  — BP TTL module
const int    BP_DIA           = 81;    // mmHg dia  — BP TTL module
const float  ROOM_TEMPERATURE = 28.3;  // °C        — DHT22
const float  HUMIDITY         = 61.5;  // %         — DHT22
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Returns current Unix epoch (seconds). Returns 0 if NTP not synced yet.
// ---------------------------------------------------------------------------
long getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 0;
  return (long)mktime(&timeinfo);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  // Non-blocking: WiFi connection happens in the background.
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Fixed client ID — avoids orphaned sessions on the broker
    String clientId = "ESP32-Demo-Gateway-1";
    if (client.connect(clientId.c_str())) {
      Serial.println("connected to MQTT broker!");
      client.publish("health/status", "ESP32 DEMO Gateway Online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" — will retry next cycle.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  // Wait briefly for WiFi so NTP sync can start right away
  unsigned long wifiWait = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiWait < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmt_offset, dst_offset, ntp_server);
    Serial.println("NTP sync started...");
  }

  Serial.println("[DEMO MODE] No physical sensors required.");
}

void loop() {
  // Keep MQTT alive when connected
  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    client.loop();
  }

  unsigned long now = millis();
  if (now - lastMsgTime > interval) {
    lastMsgTime = now;

    // ── 1. Build JSON payload ──────────────────────────────────────────────
    StaticJsonDocument<256> doc;

    doc["device_id"]        = DEVICE_ID;
    doc["timestamp"]        = getTimestamp();   // Unix epoch seconds (IST)
    doc["heart_rate"]       = HEART_RATE;
    doc["spo2"]             = SPO2;
    doc["body_temperature"] = BODY_TEMPERATURE;
    doc["room_temperature"] = ROOM_TEMPERATURE; // flat — no nested env object
    doc["humidity"]         = HUMIDITY;         // flat — no nested env object

    // blood_pressure stays nested (sys/dia are paired values)
    JsonObject bp = doc.createNestedObject("blood_pressure");
    bp["sys"] = BP_SYS;
    bp["dia"] = BP_DIA;

    // ── 2. Serialize ───────────────────────────────────────────────────────
    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    // ── 3. Always print to Serial (works even without WiFi) ───────────────
    Serial.println();
    Serial.println("===== [DEMO] SENSOR READINGS =====");
    Serial.println(jsonBuffer);

    // ── 4. Publish to RPi MQTT broker ─────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) reconnect();
      if (client.connected()) {
        bool ok = client.publish("health/sensors", jsonBuffer);
        Serial.println(ok ? "Status: Published to RPi [health/sensors] ✓"
                          : "Status: Publish failed (buffer full?)");
      } else {
        Serial.println("Status: Waiting for MQTT broker...");
      }
    } else {
      Serial.println("Status: Waiting for WiFi...");
    }
  }
}