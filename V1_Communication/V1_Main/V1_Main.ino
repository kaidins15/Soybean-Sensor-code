/*
 * RECEIVER HUB CODE (Freenove ESP32)
 * 1. Creates Wi-Fi AP "ESP32-Hub"
 * 2. Receives ESP-NOW data from 4 boards
 * 3. Hosts a Web Server to display the data
 */
#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>

// Wi-Fi Credentials for the Access Point
const char* ssid = "ESP32-Hub";
const char* password = "password123";

// Create Web Server on port 80
WebServer server(80);

// Structure must match the Sender exactly
typedef struct struct_message {
  int id;
  float temp;
  float hum;
  int battery;
  float batteryVoltage;
  int moistureRaw;
  int soilmoisture;
} struct_message;

struct_message incomingReadings;

// Storage for 4 boards (Index 0 = Board 1, Index 1 = Board 2, etc.)
struct_message boardsStruct[4];

// Flag to know if we received new data (for debugging)
bool newData = false;

// --- ESP-NOW CALLBACK ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // 1. Copy the incoming data into our structure
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
  
  // 2. Print a nice header
  Serial.print("\n--- Data Received from Board ID: ");
  Serial.print(incomingReadings.id);
  Serial.println(" ---");
  
  // 3. Print the Mac Address of the Sender
  Serial.print("Source MAC: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(macStr);

  // 4. Print the Data Values
  Serial.print("Temperature: ");
  Serial.print(incomingReadings.temp);
  Serial.println(" °C");
  
  Serial.print("Humidity:    ");
  Serial.print(incomingReadings.hum);
  Serial.println(" %");
  
  Serial.print("Battery:     ");
  Serial.print(incomingReadings.battery);
  Serial.println(" %");

  Serial.print("Battery Voltage:    ");
  Serial.print(incomingReadings.batteryVoltage);
  Serial.println(" V");
  
  Serial.print("Moisture Raw:     ");
  Serial.print(incomingReadings.moistureRaw);
  Serial.println(" ");

  Serial.print("Soil Moisture:     ");
  Serial.print(incomingReadings.soilmoisture);
  Serial.println(" %");
  
  Serial.print("Bytes Recv:  ");
  Serial.println(len);
  Serial.println("------------------------------------");
}

// --- HTML WEBPAGE ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<meta http-equiv=\"refresh\" content=\"2\">"; // Auto-refresh every 2 seconds
  html += "<style>";
  html += "body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}";
  html += "table { margin-left: auto; margin-right: auto; border-collapse: collapse; width: 90%; }";
  html += "td, th { border: 1px solid #ddd; padding: 8px; }";
  html += "tr:nth-child(even){background-color: #f2f2f2;}";
  html += "th { padding-top: 12px; padding-bottom: 12px; text-align: center; background-color: #04AA6D; color: white; }";
  html += "</style></head><body>";
  
  html += "<h1>ESP32 Sensor Hub</h1>";
  html += "<table><tr><th>Board ID</th><th>Temp (&deg;C)</th><th>Humidity (%)</th><th>Battery (%)</th><th>Battery Voltage (V)></th><th>Moisture Raw ()></th><th>Soil Moisture (%)>";
  
  for (int i = 0; i < 4; i++) {
    html += "<tr>";
    html += "<td>Board " + String(i+1) + "</td>";
    // Check if data is valid (temp isn't 0)
    if (boardsStruct[i].temp == 0 && boardsStruct[i].hum == 0) {
      html += "<td colspan='3'>No Data Yet</td>";
    } else {
      html += "<td>" + String(boardsStruct[i].temp, 1) + "</td>";
      html += "<td>" + String(boardsStruct[i].hum, 0) + "</td>";
      html += "<td>" + String(boardsStruct[i].battery) + "</td>";
      html += "<td>" + String(boardsStruct[i].batteryVoltage, 2) + " V</td>";
      html += "<td>" + String(boardsStruct[i].moistureRaw) + "</td>";
      html += "<td>" + String(boardsStruct[i].soilmoisture) + "</td>";
    }
    html += "</tr>";
  }
  
  html += "</table></body></html>";
  return html;
}
void printCurrentNetworkInfo() {
  Serial.println("\n---------------------------------------");
  Serial.println("       FREENOVE HUB STATUS REPORT      ");
  Serial.println("---------------------------------------");
  
  // 1. Wi-Fi Access Point Info
  Serial.print("Network Name (SSID):  ");
  Serial.println(ssid);  // Or WiFi.softAPSSID()
  
  Serial.print("Network Password:     ");
  Serial.println(password);
  
  Serial.print("Web Server IP:        ");
  Serial.println(WiFi.softAPIP()); // Usually 192.168.4.1

  // 2. Hardware Info (Crucial for ESP-NOW)
  Serial.print("Hub MAC Address:      ");
  Serial.println(WiFi.macAddress());
  
  Serial.print("Wi-Fi Channel:        ");
  Serial.println(WiFi.channel()); // Sender MUST match this number!
  
  Serial.println("---------------------------------------\n");
}
void setup() {
  Serial.begin(115200);

  // Set as AP (Access Point) AND Station (for ESP-NOW)
  WiFi.mode(WIFI_AP_STA);
  
  // Start the Access Point
  WiFi.softAP(ssid, password);
  
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress()); // Use this MAC in your Sender code!

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register Callback
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Start Web Server
  server.on("/", [](){
    server.send(200, "text/html", getHTML());
  });
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  // printCurrentNetworkInfo();
  // sleep(10000);
  // Handle client requests
  server.handleClient();
  
  // Optional: Print to Serial for debugging
  if (newData) {
    Serial.println("New data received!");
    newData = false;
  }
}