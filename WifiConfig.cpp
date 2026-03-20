#include "WifiConfig.h"
#include <Arduino.h>

void WifiConfigurator::begin(const char* defSsid, const char* defPass, int btnPin) {
  buttonPin = btnPin;
  defaultSsid = defSsid ? defSsid : "";
  defaultPass = defPass ? defPass : "";

  pinMode(buttonPin, INPUT_PULLUP);

  prefs.begin("wifi", false);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");

  WiFi.mode(WIFI_STA);
  if (savedSsid.length() > 0) {
    tryStation(savedSsid.c_str(), savedPass.c_str());
  } else {
    tryStation(defaultSsid.c_str(), defaultPass.c_str());
  }

  // Web handlers
  web.on("/", HTTP_GET, [this]() { serveLandingPage(); });
  web.on("/save", HTTP_POST, [this]() { handleSave(); });
}

void WifiConfigurator::loop() {
  if (apMode) {
    web.handleClient();
    return;
  }
  checkButton();
}

void WifiConfigurator::checkButton() {
  int state = digitalRead(buttonPin);
  if (state == LOW) {
    if (buttonPressedAt == 0) buttonPressedAt = millis();
    if (!apMode && millis() - buttonPressedAt > HOLD_MS) {
      startApConfigMode();
    }
  } else {
    buttonPressedAt = 0;
  }
}

void WifiConfigurator::startApConfigMode() {
  apMode = true;
  stopPlayback(true, true);  // silence audio while configuring
  WiFi.disconnect(true);
  uint32_t chip = (uint32_t)ESP.getEfuseMac();
  sprintf(apSsid, "QuranPlayer-%04X", (uint16_t)(chip & 0xFFFF));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid);
  web.begin();
  Serial.printf("[CFG] AP started: %s (http://192.168.4.1)\n", apSsid);
}

void WifiConfigurator::stopApConfigMode() {
  web.stop();
  WiFi.softAPdisconnect(true);
  apMode = false;
  WiFi.mode(WIFI_STA);
}

String WifiConfigurator::scanOptions() {
  int n = WiFi.scanNetworks();
  String out;
  for (int i = 0; i < n; i++) {
    out += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
  }
  if (n == 0) out = "<option value=''>No networks found</option>";
  return out;
}

void WifiConfigurator::serveLandingPage() {
  String page = R"rawliteral(
  <html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>body{font-family:sans-serif;margin:20px;}button,input,select{font-size:1.05em;}</style></head><body>
  <h2>Wi‑Fi Setup</h2>
  <form action='/save' method='POST'>
    <label>Network:</label><br>
    <select name='ssid'>%OPTIONS%</select><br><br>
    <label>Password:</label><br>
    <input type='password' name='pass' required><br><br>
    <button type='submit'>Save & Connect</button>
  </form>
  </body></html>)rawliteral";
  page.replace("%OPTIONS%", scanOptions());
  web.send(200, "text/html", page);
}

void WifiConfigurator::handleSave() {
  String ssid = web.hasArg("ssid") ? web.arg("ssid") : "";
  String pass = web.hasArg("pass") ? web.arg("pass") : "";
  if (ssid.length() == 0) {
    web.send(400, "text/plain", "SSID required");
    return;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  savedSsid = ssid;
  savedPass = pass;

  web.send(200, "text/plain", "Saved. Connecting...");
  delay(300);
  stopApConfigMode();
  tryStation(savedSsid.c_str(), savedPass.c_str());
}

void WifiConfigurator::tryStation(const char* ssid, const char* pass) {
  if (!ssid || strlen(ssid) == 0) return;
  WiFi.begin(ssid, pass ? pass : "");
  Serial.printf("[WiFi] Connecting to %s\n", ssid);
}
