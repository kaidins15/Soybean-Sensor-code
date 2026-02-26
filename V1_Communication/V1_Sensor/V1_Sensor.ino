/*
 * SENDER CODE (Seeed XIAO ESP32C3)
 * 1. Reads simulated sensor data
 * 2. Sends data to the Hub via ESP-NOW
 * 3. Uses random delay to prevent collisions
 */
#include <esp_now.h>
#include <WiFi.h>

// --- CONFIGURATION: CHANGE THIS FOR EACH BOARD (1, 2, 3, 4) ---
#define BOARD_ID 1

// REPLACE WITH YOUR FREENOVE HUB'S MAC ADDRESS
// EC:E3:34:A3:4F:68
uint8_t broadcastAddress[] = {0xEC, 0xE3, 0x34, 0xA3, 0x4F, 0x68};

// Structure must match the Receiver exactly
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

// Callback: verifies if data was sent successfully
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: You can add an LED blink here on success
}

const int mosfetPin = A2;
const int batteryPin = A0;
const int sensorPin = A1;
const int xiaoLed = 10; // The built-in LED on XIAO ESP32-C3 is pin 10

void setup() {
  Serial.begin(115200);
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  pinMode(mosfetPin, OUTPUT);
  pinMode(xiaoLed, OUTPUT); // Tell the XIAO we want to use the LED
  
  digitalWrite(mosfetPin, HIGH); // Sensor OFF
  digitalWrite(xiaoLed, HIGH);   // LED OFF (XIAO LED is active LOW)
  
  analogReadResolution(12); 

  // Register the Send Callback
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);  
  // Register peer (The Hub)
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  // 1. Prepare data (Replace these with actual sensor reads later)
  //myData.id = BOARD_ID;
  //myData.temp = random(200, 350) / 10.0; // Simulated Temp: 20.0 - 35.0 C
  //myData.hum = random(40, 80);           // Simulated Humidity
  //myData.battery = random(0, 100);       // Simulated Battery %

  Serial.println("----- NEW CYCLE -----");

  // 2. battery reading
  // Double check the equationi
  int battRaw = analogRead(batteryPin);
  float battVoltage = battRaw * (3.3 / 4095.0) * 3.05;

  // 3. sensor on
  digitalWrite(mosfetPin, LOW);
  delay(1000);

  // 4. read moisture data
  int moistureRaw = analogRead(sensorPin);

  //replace with calibration equation
  float dry = 3500.0;
  float wet = 1500.0;
  int soilmoisture = (int)(((int)moistureRaw - dry) / (wet - dry) * 100.0);

  if (soilmoisture > 100) soilmoisture = 100;
  if (soilmoisture < 0) soilmoisture = 0;

  // 5. sensor off
  digitalWrite(mosfetPin, HIGH);

  // 6. put value
  myData.id = BOARD_ID;
  myData.batteryVoltage = battVoltage;
  myData.moistureRaw = moistureRaw;
  myData.soilmoisture = soilmoisture;

  // 7. Send message via ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
   
  if (result == ESP_OK) {
    Serial.printf("Board %d: Sent successfully\n", BOARD_ID);
  } else {
    Serial.println("Error sending the data");
  }
  
  // 8. Anti-Collision Delay
  // Wait 2 seconds + random 0-500ms so boards don't talk at once
  delay(2000 + random(0, 500)); 
}