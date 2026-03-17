#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebSocketMCP.h>
#include <ArduinoJson.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------- SD SPI pins ----------
static const int SD_CS   = 5;
static const int SD_SCK  = 18;
static const int SD_MISO = 4;
static const int SD_MOSI = 23;

// ---------- Audio output pins ----------
static const int I2S_BCLK = 14;
static const int I2S_LRC  = 27;
static const int I2S_DIN  = 33;

// ---------- WiFi + MCP ----------
static const char* WIFI_SSID = "Mylove";
static const char* WIFI_PASS = "20032003";
static const char* MCP_ENDPOINT = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjgzMTMwMywiYWdlbnRJZCI6MTQ5ODgyMCwiZW5kcG9pbnRJZCI6ImFnZW50XzE0OTg4MjAiLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzczNTIwNDE0LCJleHAiOjE4MDUwNzgwMTR9.26AX1UBizc3KDzxR8vz6FyPkvpL0B0xf02c25viqjLdaVhwGpCzkKYoeP_145hBdeuxZzM3i3ztuncxpVQd5Mg";

// ---------- Debug ----------
#define DEBUG_SERIAL Serial
#define DEBUG_BAUD_RATE 115200
#define LED_PIN 2

// ---------- Quran playback constants ----------
static constexpr const char* QURAN_BASE_PATH = "/quran";
static constexpr int SURAH_COUNT = 114;
static constexpr int MAX_AYAH_PER_SURAH = 286;
static constexpr unsigned long DEFAULT_SURAH_END_DELAY_MS = 10000;
unsigned long surahEndDelayMs = DEFAULT_SURAH_END_DELAY_MS;

// ---------- Audio objects ----------
AudioGeneratorMP3 *mp3 = new AudioGeneratorMP3();
AudioFileSourceSD *audioFile = nullptr;
AudioOutputI2S *out = new AudioOutputI2S();
TaskHandle_t playbackTaskHandle = nullptr;

WebSocketMCP mcpClient;
bool wifiConnected = false;
bool mcpConnected = false;

struct PlaybackState {
  int surah = 0;
  int ayah = 0;
  bool autoAdvance = false;
  bool waitingForNextSurah = false;
  unsigned long waitUntil = 0;
  bool active = false;
} playback;

// ---------- Function declarations ----------
void setupWifi();
void registerMcpTools();
void onMcpConnectionChange(bool connected);
void processSerialCommands();
void handleSerialCommand(const String &command);
void printHelp();
void printStatus();
void blinkLed(int times, int delayMs);
String buildAyahPath(int surah, int ayah);
bool isValidSurah(int surah);
bool isValidAyah(int ayah);
bool startAyahPlayback(int surah, int ayah, bool continueMode);
void stopPlayback();
void handlePlaybackCompletion();
void updateAutoSurahAdvance();
int findFirstAyahInSurah(int surah);
int findNextSurahWithAyahs(int currentSurah);
bool ensureAyahExists(int surah, int ayah);
void playbackTask(void *pvParameters);

void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUD_RATE);
  delay(300);
  DEBUG_SERIAL.println("\n[ESP32 Quran Player - MAX98357A]");

  pinMode(LED_PIN, OUTPUT);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    DEBUG_SERIAL.println("SD init failed");
    while (1);
  }

  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
  out->SetGain(0.5);

  setupWifi();
  if (!mcpClient.begin(MCP_ENDPOINT, onMcpConnectionChange)) {
    DEBUG_SERIAL.println("[MCP] Initialization failed");
  }

  // Run audio loop on core 1 to isolate from WiFi/MCP activity
  xTaskCreatePinnedToCore(playbackTask, "audio_playback", 4096, nullptr, 1, &playbackTaskHandle, 1);

  DEBUG_SERIAL.println("Ready to accept commands via serial or MCP tools.");
  DEBUG_SERIAL.println("Type \"help\" in the serial console to get the control syntax.");
}

void loop() {
  mcpClient.loop();

  updateAutoSurahAdvance();
  processSerialCommands();

  if (!wifiConnected) {
    blinkLed(1, 100);
  } else if (!mcpConnected) {
    blinkLed(1, 500);
  } else if (playback.active) {
    blinkLed(2, 250);
  } else {
    digitalWrite(LED_PIN, HIGH);
  }
}

void playbackTask(void *pvParameters) {
  for (;;) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
      }
    }
    if (!mp3->isRunning() && playback.active) {
      handlePlaybackCompletion();
    }
    vTaskDelay(1);  // yield to other tasks
  }
}

void setupWifi() {
  DEBUG_SERIAL.print("[WiFi] Connecting to ");
  DEBUG_SERIAL.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.setHostname("esp32-quran");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    DEBUG_SERIAL.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    // success path
  } else {
    DEBUG_SERIAL.println("\n[WiFi] Initial attempt timed out, will keep retrying in loop.");
    wifiConnected = false;
  }


  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    DEBUG_SERIAL.println("\n[WiFi] Connected");
    DEBUG_SERIAL.print("[WiFi] IP: ");
    DEBUG_SERIAL.println(WiFi.localIP());
  } else {
    DEBUG_SERIAL.print("[WiFi] Failed with status ");
    DEBUG_SERIAL.println(WiFi.status());

    wifiConnected = false;
    DEBUG_SERIAL.println("\n[WiFi] Connection failed, will keep retrying in loop.");
  }
  DEBUG_SERIAL.print("[WiFi] Status ");
  DEBUG_SERIAL.println(WiFi.status());
  DEBUG_SERIAL.print("[WiFi] RSSI ");
  DEBUG_SERIAL.println(WiFi.RSSI());

}

void onMcpConnectionChange(bool connected) {
  mcpConnected = connected;
  if (connected) {
    DEBUG_SERIAL.println("[MCP] Connected to server");
    registerMcpTools();
  } else {
    DEBUG_SERIAL.println("[MCP] Disconnected");
  }
}

void registerMcpTools() {
  mcpClient.registerTool(
    "play_ayah",
    "Play a specific Ayah",
    "{\"properties\":{\"surah\":{\"type\":\"integer\"},\"ayah\":{\"type\":\"integer\"},\"mode\":{\"type\":\"string\",\"enum\":[\"stop\",\"continue\"]}},\"required\":[\"surah\",\"ayah\"],\"title\":\"playAyahArguments\",\"type\":\"object\"}",
    [](const String &args) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, args);
      if (error) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid JSON\"}", true);
      }

      int surah = doc["surah"];
      int ayah = doc["ayah"];
      String mode = doc.containsKey("mode") ? doc["mode"].as<String>() : "stop";
      bool continueMode = mode.equalsIgnoreCase("continue");

      if (!startAyahPlayback(surah, ayah, continueMode)) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Ayah file not found\"}", true);
      }

      String response = "{\"success\":true,\"surah\":" + String(surah) + ",\"ayah\":" + String(ayah) + ",\"mode\":\"" + mode + "\"}";
      return WebSocketMCP::ToolResponse(response);
    }
  );

  mcpClient.registerTool(
    "stop_playback",
    "Stop Quran playback",
    "{\"properties\":{},\"title\":\"stopPlaybackArguments\",\"type\":\"object\"}",
    [](const String &args) {
      stopPlayback();
      return WebSocketMCP::ToolResponse("{\"success\":true}");
    }
  );
}

void processSerialCommands() {
  while (DEBUG_SERIAL.available() > 0) {
    char inChar = (char)DEBUG_SERIAL.read();
    static char buffer[128];
    static int index = 0;

    if (inChar == '\n' || inChar == '\r') {
      if (index > 0) {
        buffer[index] = '\0';
        String command = String(buffer);
        command.trim();
        handleSerialCommand(command);
        index = 0;
      }
    } else if (inChar == '\b' || inChar == 127) {
      if (index > 0) {
        index--;
        DEBUG_SERIAL.print("\b \b");
      }
    } else if (index < sizeof(buffer) - 1) {
      buffer[index++] = inChar;
      DEBUG_SERIAL.print(inChar);
    }
  }
}

void handleSerialCommand(const String &command) {
  if (command.length() == 0) return;

  if (command.equalsIgnoreCase("help")) {
    printHelp();
    return;
  }

  if (command.equalsIgnoreCase("status")) {
    printStatus();
    return;
  }

  if (command.equalsIgnoreCase("stop")) {
    stopPlayback();
    DEBUG_SERIAL.println("Playback stopped.");
    return;
  }

  if (command.startsWith("play ")) {
    int firstSpace = command.indexOf(' ');
    String rest = command.substring(firstSpace + 1);
    rest.trim();
    int secondSpace = rest.indexOf(' ');
    if (secondSpace < 0) {
      DEBUG_SERIAL.println("Usage: play <stop|continue> <surah> <ayah>");
      return;
    }

    String modeToken = rest.substring(0, secondSpace);
    String numbers = rest.substring(secondSpace + 1);
    numbers.trim();
    int thirdSpace = numbers.indexOf(' ');
    if (thirdSpace < 0) {
      DEBUG_SERIAL.println("Usage: play <stop|continue> <surah> <ayah>");
      return;
    }

    String surahToken = numbers.substring(0, thirdSpace);
    String ayahToken = numbers.substring(thirdSpace + 1);

    int surah = surahToken.toInt();
    int ayah = ayahToken.toInt();
    bool continueMode = modeToken.equalsIgnoreCase("continue");

    if (!startAyahPlayback(surah, ayah, continueMode)) {
      DEBUG_SERIAL.printf("Failed to find Ayah %03d:%03d\n", surah, ayah);
    } else {
      DEBUG_SERIAL.printf("Playing Ayah %03d:%03d (%s)\n", surah, ayah, continueMode ? "continues" : "stops");
    }

    return;
  }

  if (command.equalsIgnoreCase("reconnect")) {
    DEBUG_SERIAL.println("Reconnecting MCP...");
    mcpClient.disconnect();
    mcpClient.begin(MCP_ENDPOINT, onMcpConnectionChange);
    return;
  }

  DEBUG_SERIAL.println("Unknown command. Type \"help\" for usage.");
}

void printHelp() {
  DEBUG_SERIAL.println("Available commands:");
  DEBUG_SERIAL.println("  help                     - Show this help");
  DEBUG_SERIAL.println("  status                   - Show current connection/playback status");
  DEBUG_SERIAL.println("  play stop <surah> <ayah> - Play a specific ayah and stop");
  DEBUG_SERIAL.println("  play continue <surah> <ayah> - Play from the given ayah to the end of surah and continue to the next surah after delay");
  DEBUG_SERIAL.println("  stop                     - Immediately stop playback");
  DEBUG_SERIAL.println("  reconnect                - Force MCP reconnect");
}

void printStatus() {
  DEBUG_SERIAL.println("Status:");
  DEBUG_SERIAL.printf("  WiFi: %s\n", wifiConnected ? "connected" : "disconnected");
  DEBUG_SERIAL.printf("  MCP: %s\n", mcpConnected ? "connected" : "disconnected");
  if (playback.active) {
    DEBUG_SERIAL.printf("  Playing Ayah %03d:%03d (mode: %s)\n", playback.surah, playback.ayah, playback.autoAdvance ? "continue" : "stop");
  } else if (playback.waitingForNextSurah) {
    DEBUG_SERIAL.println("  Waiting to start the next Surah...");
  } else {
    DEBUG_SERIAL.println("  Playback: stopped");
  }
}

void blinkLed(int times, int delayMs) {
  static int blinkCount = 0;
  static unsigned long lastBlinkAt = 0;
  static bool ledState = false;
  static int lastTimes = 0;

  if (times == 0) {
    digitalWrite(LED_PIN, LOW);
    blinkCount = 0;
    lastTimes = 0;
    return;
  }

  if (lastTimes != times) {
    blinkCount = 0;
    lastTimes = times;
    ledState = false;
    lastBlinkAt = millis();
  }

  if ((int)blinkCount >= times * 2) {
    digitalWrite(LED_PIN, LOW);
    blinkCount = 0;
    lastTimes = 0;
    return;
  }

  if (millis() - lastBlinkAt > delayMs) {
    lastBlinkAt = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    blinkCount++;
  }
}

String buildAyahPath(int surah, int ayah) {
  char pathBuffer[64];
  sprintf(pathBuffer, "%s/%03d/%03d.mp3", QURAN_BASE_PATH, surah, ayah);
  return String(pathBuffer);
}

bool isValidSurah(int surah) {
  return surah >= 1 && surah <= SURAH_COUNT;
}

bool isValidAyah(int ayah) {
  return ayah >= 1 && ayah <= MAX_AYAH_PER_SURAH;
}

bool ensureAyahExists(int surah, int ayah) {
  if (!isValidSurah(surah) || !isValidAyah(ayah)) return false;
  String path = buildAyahPath(surah, ayah);
  bool found = SD.exists(path.c_str());
  // DEBUG_SERIAL.printf("[Quran] Checking %s => %s\n", path.c_str(), found ? "found" : "missing");
  return found;
}

bool startAyahPlayback(int surah, int ayah, bool continueMode) {
  if (!ensureAyahExists(surah, ayah)) {
    // DEBUG_SERIAL.printf("[Quran] Ayah %03d:%03d not available\n", surah, ayah);
    return false;
  }

  stopPlayback();

  String path = buildAyahPath(surah, ayah);
  audioFile = new AudioFileSourceSD(path.c_str());
  mp3->begin(audioFile, out);

  playback.surah = surah;
  playback.ayah = ayah;
  playback.autoAdvance = continueMode;
  playback.waitingForNextSurah = false;
  playback.active = true;
  playback.waitUntil = 0;

  return true;
}

void stopPlayback() {
  playback.active = false;
  playback.waitingForNextSurah = false;
  playback.waitUntil = 0;
  playback.autoAdvance = false;

  if (mp3->isRunning()) {
    mp3->stop();
  }

  if (audioFile) {
    delete audioFile;
    audioFile = nullptr;
  }
}

void handlePlaybackCompletion() {
  if (!playback.active || mp3->isRunning()) return;

  // DEBUG_SERIAL.printf("[Quran] Playback completed Ayah %03d:%03d, mp3->isRunning=%d\n",
  //                     playback.surah, playback.ayah, mp3->isRunning());

  if (playback.autoAdvance) {
    int nextAyah = playback.ayah + 1;
    if (startAyahPlayback(playback.surah, nextAyah, true)) {
      return;
    }

    playback.waitingForNextSurah = true;
    playback.waitUntil = millis() + surahEndDelayMs;
    playback.active = false;
  } else {
    playback.active = false;
    if (audioFile) {
      delete audioFile;
      audioFile = nullptr;
    }
  }
}

void updateAutoSurahAdvance() {
  if (!playback.waitingForNextSurah) return;
  if (millis() < playback.waitUntil) return;

  playback.waitingForNextSurah = false;
  int nextSurah = findNextSurahWithAyahs(playback.surah);
  if (nextSurah == 0) {
    DEBUG_SERIAL.println("[Quran] No additional Surahs with audio files");
    return;
  }

  int firstAyah = findFirstAyahInSurah(nextSurah);
  if (firstAyah == 0) {
    DEBUG_SERIAL.println("[Quran] Failed to locate starting ayah for next Surah");
    return;
  }

  startAyahPlayback(nextSurah, firstAyah, true);
}

int findFirstAyahInSurah(int surah) {
  for (int ayah = 1; ayah <= MAX_AYAH_PER_SURAH; ++ayah) {
    if (ensureAyahExists(surah, ayah)) {
      return ayah;
    }
  }
  return 0;
}

int findNextSurahWithAyahs(int currentSurah) {
  int attempts = 0;
  int candidate = currentSurah;
  while (attempts < SURAH_COUNT) {
    candidate++;
    if (candidate > SURAH_COUNT) {
      candidate = 1;
    }
    if (findFirstAyahInSurah(candidate) > 0) {
      return candidate;
    }
    attempts++;
  }
  return 0;
}
