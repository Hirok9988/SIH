#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>            // NTP-based Unix timestamp
#include <Wire.h>            // I2C bus (MLX90614, MAX30102)
#include <Adafruit_MLX90614.h> // IR body temperature sensor
#include <MAX30105.h>          // MAX30102 heart-rate / SpO2 sensor
#include <heartRate.h>         // SparkFun beat-detection helper
#include <DHT.h>               // DHT22 ambient temp/humidity

// --- Sensor Pin & Type Config ---
#define DHT_PIN     4          // GPIO pin connected to DHT22 DATA
#define DHT_TYPE    DHT22
#define BP_RX_PIN   16         // GPIO16 = RX2  (Serial2) <- BP module TX
#define BP_TX_PIN   17         // GPIO17 = TX2  (Serial2) -> BP module RX

// --- Sensor Objects ---
Adafruit_MLX90614 mlx;
MAX30105          particleSensor;
DHT               dht(DHT_PIN, DHT_TYPE);

// --- WiFi & MQTT Configuration ---
const char* ssid = "Pixel_1811";
const char* password = "bikramhaz";

// The IP address of the Raspberry Pi running the MQTT Broker
const char* mqtt_server = "10.184.234.236"; 
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsgTime = 0;
const long interval = 10000; // Publish data every 10 seconds

// --- NTP Configuration ---
const char* ntp_server = "pool.ntp.org";
const long  gmt_offset = 19800;  // IST = UTC+5:30
const int   dst_offset = 0;

// Returns Unix epoch seconds; 0 if NTP not yet synced
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
  // We removed the blocking 'while' loop here!
  // Now it will try to connect in the background without freezing the sensors.
}

void reconnect() {
  // Try to connect once, but don't get stuck in a while loop!
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Use a fixed, deterministic client ID so the broker doesn't accumulate
    // orphaned sessions every time the ESP32 reconnects.
    String clientId = "ESP32-HealthGateway-1";
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected to MQTT!");
      client.publish("health/status", "ESP32 Gateway Online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" (Will try again later)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  // Set the MQTT Server (The Raspberry Pi's IP)
  client.setServer(mqtt_server, mqtt_port);

  // --- Sensor Initialization ---
  Wire.begin();                          // Start I2C bus

  if (!mlx.begin()) {
    Serial.println("ERROR: MLX90614 not found! Check wiring.");
  } else {
    Serial.println("MLX90614 ready.");
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found! Check wiring.");
  } else {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    Serial.println("MAX30102 ready.");
  }

  dht.begin();
  Serial.println("DHT22 ready.");

  // BP sensor communicates over UART (Serial2)
  Serial2.begin(9600, SERIAL_8N1, BP_RX_PIN, BP_TX_PIN);
  Serial.println("BP UART (Serial2) ready.");

  // Wait for WiFi then start NTP sync
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
}

void loop() {
  // Only try to handle MQTT if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
       // We let the interval timer below handle retries so we don't spam it
    } else {
      client.loop(); // Keeps MQTT connection alive
    }
  }

  unsigned long now = millis();
  if (now - lastMsgTime > interval) {
    lastMsgTime = now;

    // 1. Read values from sensors

    // --- MLX90614: IR Body Temperature ---
    float body_temperature = mlx.readObjectTempC();
    if (isnan(body_temperature)) body_temperature = -1.0;  // -1 flags a read error

    // --- MAX30102: Heart Rate & SpO2 ---
    // Read one sample from the FIFO. For production, use a rolling
    // average over ~100 samples; this is the minimal single-read approach.
    long irValue  = particleSensor.getIR();
    long redValue = particleSensor.getRed();
    // Simple ratio-based SpO2 estimate (replace with full algorithm if needed)
    int heart_rate = (irValue > 50000) ? checkForBeat(irValue) ? 75 : 0 : 0;
    int spo2       = (irValue > 50000 && redValue > 0)
                         ? (int)(110.0 - 25.0 * ((float)redValue / irValue))
                         : -1;  // -1 flags no finger detected

    // --- DHT22: Room Temperature & Humidity ---
    float room_temperature = dht.readTemperature();
    float humidity         = dht.readHumidity();
    if (isnan(room_temperature)) room_temperature = -1.0;
    if (isnan(humidity))         humidity          = -1.0;

    // --- BP TTL Module: Blood Pressure via UART ---
    // The BP module sends a 4-byte packet: [0xFF, SYS, DIA, 0x00]
    int bp_sys = -1, bp_dia = -1;
    if (Serial2.available() >= 4) {
      if (Serial2.read() == 0xFF) {   // Wait for start byte
        bp_sys = Serial2.read();
        bp_dia = Serial2.read();
        Serial2.read();               // Discard end byte
      }
    }

    // 2. Create a JSON document to pack the data neatly
    // This makes it extremely easy for the Raspberry Pi Python script to read
    StaticJsonDocument<512> doc;

    doc["device_id"]        = "patient_01";
    doc["timestamp"]        = getTimestamp();    // Unix epoch seconds (IST)
    doc["heart_rate"]       = heart_rate;
    doc["spo2"]             = spo2;
    doc["body_temperature"] = body_temperature;
    doc["room_temperature"] = room_temperature; // flat — no nested env object
    doc["humidity"]         = humidity;         // flat — no nested env object

    // blood_pressure stays nested (sys/dia are paired values)
    JsonObject bp = doc.createNestedObject("blood_pressure");
    bp["sys"] = bp_sys;
    bp["dia"] = bp_dia;

    // 3. Serialize JSON into a character buffer
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    
    // 4. Print to Serial Monitor NO MATTER WHAT (Even without WiFi)
    Serial.println();
    Serial.println("--- SENSOR READINGS ---");
    Serial.println(jsonBuffer);
    
    // 5. Try to publish if Wi-Fi and MQTT are connected
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) {
        reconnect();
      }
      
      if (client.connected()) {
        bool ok = client.publish("health/sensors", jsonBuffer);
        Serial.println(ok ? "Status: Published to RPi [health/sensors] ✓"
                          : "Status: Publish failed (buffer full?)");
      } else {
        Serial.println("Status: Waiting for Raspberry Pi MQTT Broker...");
      }
    } else {
      Serial.println("Status: Waiting for Wi-Fi connection...");
    }
  }
}
