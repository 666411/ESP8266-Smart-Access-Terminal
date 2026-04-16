#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Configuration ---
const char* ssid = "HUAWEI Mate60 ProMax"; // Your WiFi SSID
const char* password = "888888888"; // Your WiFi Password
const char* serverUrlBase = "http://47.99.180.214"; // !!! USE YOUR PC's LAN IP !!!

// API Endpoints
String validationEndpoint = String(serverUrlBase) + "/api/validate_qr";
String remoteOpenCheckEndpoint = String(serverUrlBase) + "/api/check_remote_open";
String logEndpoint = String(serverUrlBase) + "/api/log_entry";

// --- Hardware Pins ---
#define RELAY_PIN D5       // GPIO14 - Connecting to the 5V Active-LOW Relay IN pin
#define SCANNER_RX_PIN D7  // GPIO13 (Scanner TXD goes here)
#define SCANNER_TX_PIN D8  // GPIO15 (Scanner RXD goes here)
#define OLED_SCL D1        // GPIO5
#define OLED_SDA D2        // GPIO4

// OLED Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Scanner Serial
SoftwareSerial scannerSerial(SCANNER_RX_PIN, SCANNER_TX_PIN);

// Global Variables
unsigned long previousMillisCheckRemote = 0;
const long checkRemoteInterval = 5000; // Check every 5 seconds
String deviceId = "Device001";
bool wifiConnected = false;

// --- Function Declarations ---
void connectWiFi();
void setupOLED();
void displayMessage(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "", bool clear = true);
void resetDisplayToDefault();
void openDoor(const String& triggerSource);
void handleScannedCode(String qrCode);
void checkRemoteOpenCommand();
void logEntryToServer(const String& qrCode, const String& status);

// --- Arduino Setup Function ---
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; } // Wait for serial
  Serial.println("\n\n--- QR Access System Booting ---");

  // Initialize Relay Pin for Active LOW trigger
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // IMPORTANT: Set HIGH initially to keep the Active-LOW relay OFF
  Serial.print("Relay Initialized on Pin D5 (GPIO14)");
  Serial.println(": OFF (Active LOW configuration)");

  // Initialize OLED
  setupOLED();

  // Initialize Scanner Serial Port
  scannerSerial.begin(9600);
  scannerSerial.setTimeout(50);
  Serial.println("Scanner Serial Initialized at 9600 Baud.");

  displayMessage("System Booting...", "Wait...", deviceId, "Connecting WiFi...");
  connectWiFi();
  resetDisplayToDefault();
  Serial.println("--- System Setup Complete. Ready. ---");
}

// --- Arduino Loop Function ---
void loop() {
  // WiFi Connection Management
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) { Serial.println("WiFi Disconnected!"); displayMessage("WiFi Error!", "Lost Connect", "Reconnecting...", "", true); wifiConnected = false; }
    connectWiFi();
    if (!wifiConnected) return;
  } else {
      if (!wifiConnected) { Serial.println("WiFi Reconnected!"); resetDisplayToDefault(); wifiConnected = true; }
  }

  // Check for QR Code
  if (scannerSerial.available() > 0) {
    String qrCodeData = scannerSerial.readStringUntil('\r'); qrCodeData.trim();
    if (qrCodeData.length() > 2) {
      Serial.print("QR Data: ["); Serial.print(qrCodeData); Serial.println("]");
      while(scannerSerial.available() > 0) { scannerSerial.read(); } // Clear buffer
      displayMessage("QR Received", qrCodeData.substring(0, 16), "Validating...", "", true);
      handleScannedCode(qrCodeData);
    } else { while(scannerSerial.available() > 0) { scannerSerial.read(); } } // Clear fragments
  }

  // Check for Remote Open Command
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisCheckRemote >= checkRemoteInterval) {
    previousMillisCheckRemote = currentMillis;
    if (wifiConnected) { checkRemoteOpenCommand(); }
  }

  // Yield for ESP tasks
  { yield(); }
}


// --- Function Implementations ---

void connectWiFi() {
  Serial.print("Connecting to WiFi: "); Serial.print(ssid);
  WiFi.mode(WIFI_STA); WiFi.begin(ssid, password); int attempts = 0; Serial.print(" [");
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { delay(500); Serial.print("."); attempts++; } Serial.println("]");
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true; Serial.println("\nWiFi Connected!"); Serial.print("IP: "); Serial.println(WiFi.localIP()); Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    displayMessage("WiFi Connected!", "IP:", WiFi.localIP().toString(), serverUrlBase, true); delay(2500);
  } else { wifiConnected = false; Serial.println("\nWiFi Failed! Restarting..."); displayMessage("WiFi Failed!", "Check Config.", "Restarting...", "", true); delay(5000); ESP.restart(); }
}

void setupOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS, false, false)) { Serial.println(F("!! OLED Init Failed !!")); }
  else { Serial.println(F("OLED Initialized.")); display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0); display.cp437(true); display.println("OLED OK"); display.display(); delay(1500); display.clearDisplay(); display.display(); }
}

void displayMessage(const String& line1, const String& line2, const String& line3, const String& line4, bool clear) {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS, false, false)) { return; }
  if (clear) display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
  display.println(line1); if (line2.length() > 0) display.println(line2); if (line3.length() > 0) display.println(line3); if (line4.length() > 0) display.println(line4);
  display.display();
}

void resetDisplayToDefault() {
  // 从全局变量获取服务器 URL
  String serverAddress = String(serverUrlBase);
  // 移除协议头，让显示更简洁，更能放得下
  serverAddress.replace("http://", "");
  serverAddress.replace("https://", ""); // 以防万一以后用 https
  // 如果 URL 末尾有斜杠，也移除掉 (虽然 base URL 一般没有)
  if (serverAddress.endsWith("/")) {
       serverAddress = serverAddress.substring(0, serverAddress.length() - 1);
  }

  // 在第三行显示处理后的服务器地址
  displayMessage("QR Access System", "Scan QR Code", serverAddress, "DevID: " + deviceId, true);
}


// --- THIS FUNCTION CONTROLS THE RELAY ---
void openDoor(const String& triggerSource) {
  Serial.print("Access Granted via "); Serial.print(triggerSource); Serial.println(". Opening Door...");
  String statusMsg = (triggerSource == "QR" ? "Scan OK" : "Remote Open");
  displayMessage("Access Granted!", "Welcome!", "", statusMsg, true);

  // --- Active LOW Relay Control ---
  digitalWrite(RELAY_PIN, LOW);  // Set pin LOW to ACTIVATE the relay
  Serial.println("Relay Activated (Pin LOW)");
  delay(3000);                   // Door open duration
  digitalWrite(RELAY_PIN, HIGH); // Set pin HIGH to DEACTIVATE the relay
  Serial.println("Relay Deactivated (Pin HIGH)");
  // --- End Relay Control ---

  Serial.println("Door Closed.");
  displayMessage("Door Closed", "Ready...", "", "", true); delay(2000);
  resetDisplayToDefault();
}

void handleScannedCode(String qrCode) {
  if (!wifiConnected) { Serial.println("No WiFi"); displayMessage("WiFi Error!", "No Connect", "Retry Scan", "", true); return; }
  WiFiClient client; HTTPClient http;
  Serial.print("[HTTP] POST Validate: "); Serial.println(validationEndpoint);
  http.setTimeout(10000);
  if (http.begin(client, validationEndpoint)) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String postData = "qr_code=" + qrCode + "&device_id=" + deviceId;
      Serial.print("[HTTP] Data: "); Serial.println(postData);
      int httpCode = http.POST(postData);
      if (httpCode > 0) {
          Serial.printf("[HTTP] Resp: %d\n", httpCode); String payload = http.getString(); payload.trim(); Serial.print("[HTTP] Payload: ["); Serial.print(payload); Serial.println("]");
          if (httpCode == HTTP_CODE_OK) {
              if (payload.equalsIgnoreCase("VALID")) { logEntryToServer(qrCode, "Granted"); openDoor("QR"); }
              else { Serial.println("QR Invalid/Expired."); displayMessage("Access Denied", "Invalid Code", qrCode.substring(0,16), "", true); logEntryToServer(qrCode, "Denied"); delay(3000); resetDisplayToDefault(); }
          } else { Serial.printf("Server Error: %d\n", httpCode); displayMessage("Valid Error", "Server Issue", "Code: " + String(httpCode), payload.substring(0,16), true); delay(3000); resetDisplayToDefault(); }
      } else { String errorStr=http.errorToString(httpCode); String shortErr=errorStr.substring(0,16); Serial.printf("POST Fail: %s\n", errorStr.c_str()); displayMessage("Valid Error", "Connect Fail", shortErr, "", true); delay(3000); resetDisplayToDefault(); }
      http.end();
  } else { Serial.println("[HTTP] Connect Fail!"); displayMessage("Valid Error", "Cannot Connect", "Check Server", "", true); delay(3000); resetDisplayToDefault(); }
}

void checkRemoteOpenCommand() {
   if (!wifiConnected) return;
   WiFiClient client; HTTPClient http;
   String url = remoteOpenCheckEndpoint + "?device_id=" + deviceId;
   http.setTimeout(5000);
   if(http.begin(client, url)) {
     int httpCode = http.GET();
     if (httpCode == HTTP_CODE_OK) {
       String payload = http.getString(); payload.trim();
       if (payload.equalsIgnoreCase("OPEN")) { Serial.println("Remote Open received!"); logEntryToServer("REMOTE_ADMIN", "Granted"); openDoor("Remote"); }
     } // Silently ignore other codes
     http.end();
   } // Silently ignore connection fail
}

void logEntryToServer(const String& qrCode, const String& status) {
    if (!wifiConnected) { Serial.println("No WiFi to log."); return; }
    WiFiClient client; HTTPClient http;
    Serial.print("[HTTP] POST Log: "); Serial.println(logEndpoint);
    http.setTimeout(5000);
    if(http.begin(client, logEndpoint)) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        String postData = "qr_code=" + qrCode + "&device_id=" + deviceId + "&status=" + status;
        Serial.print("[HTTP] Log Data: "); Serial.println(postData);
        int httpCode = http.POST(postData);
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) { String p=http.getString(); if(p.equalsIgnoreCase("LOGGED")) Serial.println("Log OK."); else Serial.printf("Log OK, unexpected: %s\n", p.c_str()); }
            else { String p=http.getString(); Serial.printf("Log fail ServErr(%d): %s\n", httpCode, p.c_str()); }
        } else { Serial.printf("Log fail Err: %s\n", http.errorToString(httpCode).c_str()); }
        http.end();
     } else { Serial.println("Cannot connect log server!"); }
}
