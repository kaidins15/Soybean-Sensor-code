/*
 * RECEIVER HUB CODE (Freenove ESP32)
 * 1. WiFi AP & ESP-NOW Receiver
 * 2. Real-time Serial Dashboard for 4 Sensors
 * 3. Automatic 10s Watering & Consumption Tracking
 */
#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>

#define PUMP_ON LOW
#define PUMP_OFF HIGH

// --- CONFIGURATION ---
const int threshold = 70; 
const int PUMP_A = 25;
const int PUMP_B = 26;
float flowRate = 0.02; // L/s
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

// --- DASHBOARD PRINT FUNCTION ---
void printSerialReport() {
  Serial.println("\n======================================================================");
  Serial.println("                     SMART WATERING SYSTEM STATUS                     ");
  Serial.println("======================================================================");
  
  // Group A Summary
  Serial.printf(" [Group A]  Sensor 1: %3d%%  |  Sensor 2: %3d%%  -->  AVG: %4.1f%%\n", 
                boardsStruct[0].soilmoisture, boardsStruct[1].soilmoisture, avgA);
  Serial.printf("            PUMP A STATUS: %-10s | CUMULATIVE WATER: %.3f L\n", 
                (isPumpAActive ? "WATERING" : "STANDBY"), totalWaterA);
  
  Serial.println("----------------------------------------------------------------------");
  
  // Group B Summary
  Serial.printf(" [Group B]  Sensor 3: %3d%%  |  Sensor 4: %3d%%  -->  AVG: %4.1f%%\n", 
                boardsStruct[2].soilmoisture, boardsStruct[3].soilmoisture, avgB);
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

  // Print report when new data arrives
  printSerialReport();
}

// --- HTML WEBPAGE ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<meta http-equiv=\"refresh\" content=\"5\">";
  html += "<style>body{font-family:Arial; text-align:center; background-color:#f4f4f9;} ";
  html += "table{margin:auto; border-collapse:collapse; width:90%; background-color:white;} ";
  html += "td,th{border:1px solid #ddd; padding:12px;} ";
  html += "th{background-color:#04AA6D; color:white;} ";
  html += ".status-on{color:red; font-weight:bold;} .status-off{color:gray;}</style></head><body>";
  
  html += "<h1>ESP32 Watering Dashboard</h1>";
  html += "<h3>Cumulative Usage: A: " + String(totalWaterA, 3) + "L | B: " + String(totalWaterB, 3) + "L</h3>";
  
  html += "<table><tr><th>Test Bed</th><th>Individual Sensors</th><th>Average</th><th>Pump Status</th></tr>";
  
  // Row A
  html += "<tr><td>Group A (1&2)</td><td>" + String(boardsStruct[0].soilmoisture) + "% / " + String(boardsStruct[1].soilmoisture) + "%</td>";
  html += "<td>" + String(avgA,1) + "%</td>";
  html += "<td class=\"" + String(isPumpAActive ? "status-on" : "status-off") + "\">" + String(isPumpAActive ? "WATERING" : "IDLE") + "</td></tr>";
  
  // Row B
  html += "<tr><td>Group B (3&4)</td><td>" + String(boardsStruct[2].soilmoisture) + "% / " + String(boardsStruct[3].soilmoisture) + "%</td>";
  html += "<td>" + String(avgB,1) + "%</td>";
  html += "<td class=\"" + String(isPumpBActive ? "status-on" : "status-off") + "\">" + String(isPumpBActive ? "WATERING" : "IDLE") + "</td></tr>";
  
  html += "</table></body></html>";
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
  server.handleClient();
  controlPumps();
}