/*
 * RECEIVER HUB CODE (Freenove ESP32)
 * 1. WiFi AP & ESP-NOW Receiver
 * 2. Real-time Serial Dashboard for 4 Sensors
 * 3. Automatic 10s Watering & Consumption Tracking
 */
#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h> // MQTT Library

#define PUMP_ON LOW
#define PUMP_OFF HIGH

const char* wifi_ssid = "eduroam";
const char* eap_identity = "yka106@sfu.ca.ac.kr";
const char* eap_password = "Kyj65130327!";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "capstone/1";

const char* ap_ssid = "ESP32-Hub";
const char* ap_password = "password123";

WiFiClient espClient;
PubSubClient client(espClient);

// --- CONFIGURATION ---
const int threshold = 70; 
const int PUMP_A = 25;
const int PUMP_B = 26;
float flowRate = 0.065; // L/s
const unsigned long WATERING_DURATION = 10000; // 10 seconds

// --- GLOBAL VARIABLES ---
float avgA = 0;
float avgB = 0;

unsigned long pumpA_startMillis = 0;
bool isPumpAActive = false;
float totalWaterA = 0.0;

unsigned long pumpB_startMillis = 0;
bool isPumpBActive = false;
float totalWaterB = 0.0;

bool warningA = false;
bool warningB = false;

const char* ssid = "ESP32-Hub";
const char* password = "password123";

WebServer server(80);

typedef struct struct_message {
  int id;
  float temp;
  float hum;
  int battery;
  float batteryVoltage;
  int moistureRaw;
  int soilmoisture;
} struct_message;

struct_message boardsStruct[4];

// --- MQTT RECONNECT FUNCTION ---
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_Hub_Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// --- DASHBOARD PRINT FUNCTION ---
void printSerialReport() {
  Serial.println("\n======================================================================");
  Serial.println("                     SMART WATERING SYSTEM STATUS                     ");
  Serial.println("======================================================================");
  
  // Group A Summary (Sensor 1 & 2)
  Serial.printf(" [Group A] S1: %3d%% (Bat:%3d%%) | S2: %3d%% (Bat:%3d%%) | AVG: %4.1f%%\n", 
                boardsStruct[0].soilmoisture, boardsStruct[0].battery,
                boardsStruct[1].soilmoisture, boardsStruct[1].battery, avgA);
  
  Serial.printf("            PUMP A STATUS: %-10s | CUMULATIVE WATER: %.3f L\n", 
                (isPumpAActive ? "WATERING" : "STANDBY"), totalWaterA);
  
  Serial.println("----------------------------------------------------------------------");
  
  // Group B Summary (Sensor 3 & 4)
  Serial.printf(" [Group B] S3: %3d%% (Bat:%3d%%) | S4: %3d%% (Bat:%3d%%) | AVG: %4.1f%%\n", 
                boardsStruct[2].soilmoisture, boardsStruct[2].battery,
                boardsStruct[3].soilmoisture, boardsStruct[3].battery, avgB);
  Serial.printf("            PUMP B STATUS: %-10s | CUMULATIVE WATER: %.3f L\n", 
                (isPumpBActive ? "WATERING" : "STANDBY"), totalWaterB);
  
  Serial.println("======================================================================\n");
}

// --- PUMP CONTROL LOGIC ---
void controlPumps() {
  unsigned long currentMillis = millis();
  bool stateChanged = false;

  // Check Group A Timer
  if (isPumpAActive && (currentMillis - pumpA_startMillis >= WATERING_DURATION)) {
    digitalWrite(PUMP_A, PUMP_OFF);
    isPumpAActive = false;
    totalWaterA += (WATERING_DURATION / 1000.0) * flowRate;
    Serial.println(">>> INFO: Group A Watering Cycle Finished.");
    stateChanged = true;
  }

  // Check Group B Timer
  if (isPumpBActive && (currentMillis - pumpB_startMillis >= WATERING_DURATION)) {
    digitalWrite(PUMP_B, PUMP_OFF);
    isPumpBActive = false;
    totalWaterB += (WATERING_DURATION / 1000.0) * flowRate;
    Serial.println(">>> INFO: Group B Watering Cycle Finished.");
    stateChanged = true;
  }

  // If a pump just stopped, print the final report once
  if (stateChanged) {
    printSerialReport();
  }
}

// --- ESP-NOW CALLBACK ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  struct_message incoming;
  memcpy(&incoming, incomingData, sizeof(incoming));
  
  int index = incoming.id - 1;
  if (index >= 0 && index < 4) {
    boardsStruct[index] = incoming;
  }

  // Update Averages
  avgA = (boardsStruct[0].soilmoisture + boardsStruct[1].soilmoisture) / 2.0;
  avgB = (boardsStruct[2].soilmoisture + boardsStruct[3].soilmoisture) / 2.0;

  // --- NEW: Check for 20% difference between sensors ---
  warningA = abs(boardsStruct[0].soilmoisture - boardsStruct[1].soilmoisture) >= 20;
  warningB = abs(boardsStruct[2].soilmoisture - boardsStruct[3].soilmoisture) >= 20;

  // Trigger Logic
  if (avgA < threshold && !isPumpAActive) {
    digitalWrite(PUMP_A, PUMP_ON);
    pumpA_startMillis = millis();
    isPumpAActive = true;
    Serial.println(">>> ACTION: Group A Under Threshold. Pump ON.");
  }

  if (avgB < threshold && !isPumpBActive) {
    digitalWrite(PUMP_B, PUMP_ON);
    pumpB_startMillis = millis();
    isPumpBActive = true;
    Serial.println(">>> ACTION: Group B Under Threshold. Pump ON.");
  }

  // --- Sending INFO to MQTT ---
  if (client.connected()) {
    String payload = "{";
    payload += "\"s1\":" + String(boardsStruct[0].soilmoisture) + ",";
    payload += "\"s2\":" + String(boardsStruct[1].soilmoisture) + ",";
    payload += "\"s3\":" + String(boardsStruct[2].soilmoisture) + ",";
    payload += "\"s4\":" + String(boardsStruct[3].soilmoisture) + ",";
    payload += "\"b1\":" + String(boardsStruct[0].battery) + ",";
    payload += "\"b2\":" + String(boardsStruct[1].battery) + ",";
    payload += "\"b3\":" + String(boardsStruct[2].battery) + ",";
    payload += "\"b4\":" + String(boardsStruct[3].battery) + ",";
    payload += "\"adc1\":" + String(boardsStruct[0].moistureRaw) + ",";
    payload += "\"adc2\":" + String(boardsStruct[1].moistureRaw) + ",";
    payload += "\"adc3\":" + String(boardsStruct[2].moistureRaw) + ",";
    payload += "\"adc4\":" + String(boardsStruct[3].moistureRaw) + ",";
    payload += "\"avgA\":" + String(avgA) + ",";
    payload += "\"avgB\":" + String(avgB) + ",";
    payload += "\"pumpA\":\"" + String(isPumpAActive ? "ON" : "OFF") + "\",";
    payload += "\"pumpB\":\"" + String(isPumpBActive ? "ON" : "OFF") + "\",";
    payload += "\"waterA\":" + String(totalWaterA, 3) + ",";
    payload += "\"waterB\":" + String(totalWaterB, 3);

    payload += "}";

    client.publish(mqtt_topic, payload.c_str());
    Serial.println(">>> MQTT Sent: " + payload);
  }

  // Print report when new data arrives
  printSerialReport();
}

// --- HTML WEBPAGE ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<title>HydroSoilSense Smart Irrigation System</title>";
  html += "<style>";
  html += "body { font-family: sans-serif; background: #f0f4f8; padding: 20px; color: #333; }";
  html += ".container { max-width: 900px; margin: auto; }";
  html += "header { display: flex; justify-content: space-between; align-items: center; background: white; padding: 20px; border-radius: 15px; margin-bottom: 25px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); }";
  html += ".alert-strip { background: #e74c3c; color: white; padding: 15px; border-radius: 10px; margin-bottom: 10px; text-align: center; font-weight: bold; }";
  html += ".card-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 25px; }";
  html += ".card { background: white; border-radius: 20px; padding: 25px; box-shadow: 0 10px 20px rgba(0,0,0,0.05); border-top: 5px solid #2ecc71; }";
  html += ".card-b { border-top: 5px solid #3498db; }";
  html += ".label { font-size: 14px; color: #7f8c8d; text-transform: uppercase; font-weight: 600; }";
  html += ".value-main { font-size: 48px; font-weight: 800; color: #2c3e50; margin: 10px 0; }";
  html += ".unit { font-size: 18px; color: #95a5a6; }";
  html += ".status-row { display: flex; justify-content: space-between; align-items: center; margin-top: 20px; padding-top: 15px; border-top: 1px solid #f1f1f1; }";
  html += ".badge { padding: 6px 12px; border-radius: 50px; font-size: 12px; font-weight: bold; }";
  html += ".bg-run { background: #dff9fb; color: #0984e3; }";
  html += ".bg-idle { background: #f1f2f6; color: #747d8c; }";
  html += "</style></head><body><div class='container'>";
  
  html += "<header><h1>SMART IRRIGATION CONTROL</h1><div class='badge bg-idle'>GATEWAY ONLINE</div></header>";

  // --- DETAILED ALERTS ---
  if (warningA) {
    html += "<div class='alert-strip'>⚠️ SENSOR DISCREPANCY DETECTED: CHECK SENSOR 1 & 2</div>";
  }
  if (warningB) {
    html += "<div class='alert-strip'>⚠️ SENSOR DISCREPANCY DETECTED: CHECK SENSOR 3 & 4</div>";
  }

  html += "<div class='card-grid'>";

  // Testbed A (Sensor 1 & 2)
  html += "<div class='card'><div class='label'>Testbed A (Sensor 1 & 2)</div>";
  html += "<div class='value-main'>" + String(avgA, 1) + "<span class='unit'>% Soil Moisture</span></div>";
  html += "<div class='status-row'><span class='badge " + String(isPumpAActive ? "bg-run'>PUMPING" : "bg-idle'>SYSTEM READY") + "</span>";
  html += "<span>Used: <b>" + String(totalWaterA, 2) + " L</b></span></div></div>";

  // Testbed B (Sensor 3 & 4)
  html += "<div class='card card-b'><div class='label'>Testbed B (Sensor 3 & 4)</div>";
  html += "<div class='value-main'>" + String(avgB, 1) + "<span class='unit'>% Soil Moisture</span></div>";
  html += "<div class='status-row'><span class='badge " + String(isPumpBActive ? "bg-run'>PUMPING" : "bg-idle'>SYSTEM READY") + "</span>";
  html += "<span>Used: <b>" + String(totalWaterB, 2) + " L</b></span></div></div>";
  
  html += "</div></div></body></html>";
  return html;
}

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_A, OUTPUT);
  pinMode(PUMP_B, OUTPUT);
  digitalWrite(PUMP_A, PUMP_OFF);
  digitalWrite(PUMP_B, PUMP_OFF);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  WiFi.softAP(ap_ssid, ap_password);

  WiFi.disconnect(true); 
  WiFi.begin(wifi_ssid, WPA2_AUTH_PEAP, eap_identity, eap_identity, eap_password); 

  Serial.print("Connecting to eduroam");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  client.setServer(mqtt_server, 1883);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  server.on("/", [](){
    server.send(200, "text/html", getHTML());
  });
  server.begin();
  Serial.println("System Initialized.");
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  server.handleClient();
  controlPumps();
}