/*
 * SENDER CODE (Seeed XIAO ESP32C3) - BOARD 1
 * 
 * TO CHANGE FOR EACH SENSOR:
 * - Line 14: Change #define BOARD_ID to 2, 3, or 4
 * 
 * That's it! Everything else stays the same for all sensors.
 * 
 * AUTO CHANNEL FIX: Scans for "ESP32-StormHub" AP and automatically
 * uses whatever channel it's on. No more hardcoded channel numbers.
 */

#include "esp_wifi.h"
#include <esp_now.h>
#include <WiFi.h>

// --- ONLY CHANGE THIS FOR EACH BOARD (1, 2, 3, 4) ---
#define BOARD_ID 1

// Hub AP MAC address and name
uint8_t broadcastAddress[] = {0xEC, 0xE3, 0x34, 0xA3, 0x4F, 0x69};
const char* hubSSID = "ESP32-StormHub";

typedef struct struct_message {
  int id;
  float temp;
  float hum;
  int battery;
  float batteryVoltage;
  int moistureRaw;
  int soilmoisture;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Optional: LED blink on success
}

const int mosfetPin = A2;
const int batteryPin = A0;
const int sensorPin  = A1;
const int xiaoLed    = 10;

// --- AUTO CHANNEL DETECTION ---
// Scans for the hub AP and returns its channel
// Returns -1 if hub not found
int getHubChannel() {
  Serial.println("Scanning for hub AP...");
  int numNetworks = WiFi.scanNetworks();
  
  for (int i = 0; i < numNetworks; i++) {
    if (WiFi.SSID(i) == hubSSID) {
      int channel = WiFi.channel(i);
      Serial.printf("Found %s on channel %d\n", hubSSID, channel);
      WiFi.scanDelete(); // Free memory
      return channel;
    }
  }
  
  Serial.println("Hub AP not found! Using default channel 1.");
  WiFi.scanDelete();
  return 1; // Default fallback
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting up ...");

  WiFi.mode(WIFI_STA);

  // AUTO CHANNEL: Scan for hub and get its channel
  int hubChannel = getHubChannel();
  Serial.printf("Using channel: %d\n", hubChannel);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Set channel to match hub
  esp_wifi_set_channel(hubChannel, WIFI_SECOND_CHAN_NONE);

  pinMode(mosfetPin, OUTPUT);
  pinMode(xiaoLed, OUTPUT);
  digitalWrite(mosfetPin, HIGH); // Sensor OFF
  digitalWrite(xiaoLed, HIGH);   // LED OFF (active LOW)

  analogReadResolution(12);

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = hubChannel; // Use auto-detected channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.printf("Board %d ready on channel %d\n", BOARD_ID, hubChannel);
}

void loop() {
  Serial.println("----- NEW CYCLE -----");

  // --- Battery Reading ---
  int battRaw = analogRead(batteryPin);
  float battVoltage = battRaw * (3.3 / 4095.0) * 3.05;
  int batteryPct = (int)((battVoltage - 3.3) / (4.2 - 3.3) * 100.0);
  batteryPct = constrain(batteryPct, 0, 100);

  // --- Sensor ON ---
  digitalWrite(mosfetPin, LOW);
  delay(1000);

  // --- Read Moisture ---
  int moistureRaw = analogRead(sensorPin);

  // TODO: Replace CAL_WET with your actual calibrated wet reading per sensor
  const float CAL_DRY = 4095.0;
  const float CAL_WET = 1500.0;
  int soilmoisture = (int)((CAL_DRY - moistureRaw) / (CAL_DRY - CAL_WET) * 100.0);
  soilmoisture = constrain(soilmoisture, 0, 100);

  Serial.print("Raw Value: ");
  Serial.print(moistureRaw);
  Serial.print(" | Soil Moisture: ");
  Serial.print(soilmoisture);
  Serial.println("%");

  // --- Sensor OFF ---
  digitalWrite(mosfetPin, HIGH);

  // --- Package Data ---
  myData.id             = BOARD_ID;
  myData.battery        = batteryPct;
  myData.batteryVoltage = battVoltage;
  myData.moistureRaw    = moistureRaw;
  myData.soilmoisture   = soilmoisture;

  // --- Send via ESP-NOW ---
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  if (result == ESP_OK) {
    Serial.printf("Board %d: Sent successfully\n", BOARD_ID);
  } else {
    Serial.println("Error sending the data");
  }

  // --- Wait for transmission then sleep ---
  delay(2000);

  // Sleep duration options (uncomment one):
  uint64_t sleep_duration = 60ULL * 60ULL * 1000000ULL;   // 5 min (TESTING)
  // uint64_t sleep_duration = 30ULL * 60ULL * 1000000ULL;  // 30 min (DEPLOYMENT)
  // uint64_t sleep_duration = 60ULL * 60ULL * 1000000ULL;  // 1 hour (DEPLOYMENT)

  Serial.printf("Going to sleep for %llu seconds...\n", sleep_duration / 1000000ULL);
  esp_sleep_enable_timer_wakeup(sleep_duration);
  esp_deep_sleep_start();
}
