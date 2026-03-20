#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// Provided by main sketch
void stopPlayback(bool resetRepeat, bool resetBasmala);

class WifiConfigurator {
public:
  void begin(const char* defaultSsid, const char* defaultPass, int buttonPin = 0);
  void loop();                  // call every loop(); handles button and web UI
  bool inApMode() const { return apMode; }

private:
  void startApConfigMode();
  void stopApConfigMode();
  void checkButton();
  void serveLandingPage();
  void handleSave();
  String scanOptions();
  void tryStation(const char* ssid, const char* pass);

  Preferences prefs;
  WebServer web{80};
  int buttonPin = 0;
  bool apMode = false;
  unsigned long buttonPressedAt = 0;
  static constexpr unsigned long HOLD_MS = 7000;
  String savedSsid;
  String savedPass;
  String defaultSsid;
  String defaultPass;
  char apSsid[32];
};

#endif
