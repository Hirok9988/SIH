# Implementation Plan: Personal Health Companion (IoT Hardware Version)

This document outlines the implementation plan for the Personal Health Companion, specifically tailored for the custom hardware architecture utilizing an ESP32 sensor node and a Raspberry Pi Edge Hub.

## 1. System Architecture & Technology Stack

The system is designed as a distributed IoT network prioritizing local processing, offline functionality, and low latency using MQTT.

### Hardware Components
*   **Sensor Node (Data Acquisition):** ESP32 Microcontroller
    *   **Body Temperature:** MLX90614ESF (I2C)
    *   **Heart Rate & SpO2:** MAX30102 (I2C)
    *   **Blood Pressure:** Robosap Digital BP Sensor-TTL (UART via voltage divider/logic converter)
    *   **Environmental (Temp/Hum):** DHT22 (Digital 1-Wire)
*   **Edge AI & Processing Hub:** Raspberry Pi 4/5
    *   Hosts the MQTT Broker, Local Database, and Edge ML Models.

### Software Stack
*   **Communication Protocol:** MQTT (Message Queuing Telemetry Transport) via Wi-Fi.
*   **ESP32 Firmware:** C++ (Arduino Core) utilizing `PubSubClient` for MQTT and `ArduinoJson` for data structuring.
*   **Raspberry Pi Services:**
    *   **Broker:** Eclipse Mosquitto.
    *   **Backend/AI:** Python (using `paho-mqtt` for receiving data and TensorFlow Lite/Scikit-learn for anomaly detection).
    *   **Database:** SQLite or InfluxDB (optimized for time-series sensor data).
    *   **Dashboard:** Node-RED or a local web server (e.g., Flask/React) for visualizing vitals.

---

## 2. Phased Development Roadmap

### Phase 1: Hardware Assembly & Sensor Calibration (Weeks 1-2)
*   **Objective:** Wire all sensors to the ESP32 and verify raw data acquisition.
*   **Tasks:**
    *   Implement I2C scanning to verify MAX30102 and MLX90614 connectivity.
    *   Set up a voltage divider for the 5V BP Sensor TX pin to safely connect to the 3.3V ESP32 RX pin.
    *   Write isolated test sketches for each sensor to calibrate readings and ensure stable data streams.

### Phase 2: Communication Infrastructure (Weeks 3-4)
*   **Objective:** Establish robust, two-way communication between the ESP32 and Raspberry Pi.
*   **Tasks:**
    *   Install Mosquitto MQTT broker on the Raspberry Pi.
    *   Implement the provided `ESP32_Gateway.ino` sketch to connect to the broker.
    *   Integrate actual sensor reading logic into the sketch, pack it into JSON, and publish to `health/vitals/patient_01`.
    *   Write a basic Python subscriber script on the Pi to verify data reception.

### Phase 3: Data Storage & Dashboarding (Weeks 5-6)
*   **Objective:** Store historical data and visualize it locally.
*   **Tasks:**
    *   Set up an SQLite or InfluxDB database on the Raspberry Pi.
    *   Create a Python daemon that continuously subscribes to the MQTT topic and logs data into the database.
    *   Develop a local web dashboard (e.g., using Node-RED or Streamlit) to display real-time graphs of HR, SpO2, Temperature, and BP.

### Phase 4: Edge AI Anomaly Detection (Weeks 7-10)
*   **Objective:** Implement on-device intelligence to detect health risks (heat stress, abnormal HR, etc.).
*   **Tasks:**
    *   Gather baseline health data using the working prototype.
    *   Develop lightweight Python-based anomaly detection models (e.g., Isolation Forests or rules-based heuristic models for detecting sudden spikes in HR combined with high ambient temperature).
    *   Deploy these models on the Raspberry Pi to analyze the incoming MQTT stream in real-time.

### Phase 5: Alerts & Emergency Fallbacks (Weeks 11-12)
*   **Objective:** Ensure the user (or caregivers) are notified of health risks even in disaster scenarios.
*   **Tasks:**
    *   Implement logic on the Raspberry Pi to trigger physical alerts (e.g., sending a command back to the ESP32 to buzz a vibration motor) when an anomaly is detected.
    *   (Optional) Integrate a GSM/LTE module (like SIM800L) directly to the Raspberry Pi or ESP32 to send SMS SOS alerts if local Wi-Fi/Internet drops completely.

---

## 3. Key Challenges & Mitigation Strategies

| Challenge | Mitigation Strategy |
| :--- | :--- |
| **5V to 3.3V Logic Conversion** | Use a dedicated bi-directional logic level converter between the BP sensor and ESP32 to prevent hardware damage. |
| **Wi-Fi Instability** | The ESP32 firmware includes an automatic `reconnect()` loop. Use MQTT QoS levels to ensure dropped packets are queued and sent when reconnected. |
| **Sensor I2C Conflicts** | Fortunately, MAX30102 and MLX90614 have different default addresses. Ensure wire lengths are short to prevent I2C bus capacitance issues. |
