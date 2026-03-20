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
// Delay between finishing one surah and starting the next (ms). Lower to shorten the pause.
static constexpr unsigned long DEFAULT_SURAH_END_DELAY_MS = 1500;
unsigned long surahEndDelayMs = DEFAULT_SURAH_END_DELAY_MS;
static constexpr int BASMALA_SURAH = 1;
static constexpr int BASMALA_AYAH = 1;

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
  bool basmalaPending = false;
  int pendingSurah = 0;
  int pendingAyah = 0;
  bool pendingAutoAdvance = false;
  bool playingBasmala = false;
  // Range limiting
  bool rangeActive = false;
  int rangeEndAyah = 0;
  int rangeCountRemaining = 0; // number of additional ayahs to play in a count-based range
  bool rangeContinueAfter = false; // if true, keep continuing after range ends
} playback;

inline void clearRangeLimits() {
  playback.rangeActive = false;
  playback.rangeEndAyah = 0;
  playback.rangeCountRemaining = 0;
  playback.rangeContinueAfter = false;
}

// Surah repeat control
bool repeatSurahMode = false;
int repeatSurahId = 0;
int repeatSurahRemaining = 0;  // additional runs after the current one

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
bool startAyahPlayback(int surah, int ayah, bool continueMode, bool allowBasmala = true);
void stopPlayback(bool resetRepeat = true, bool resetBasmala = true);
void handlePlaybackCompletion();
void updateAutoSurahAdvance();
int findFirstAyahInSurah(int surah);
int findNextSurahWithAyahs(int currentSurah);
bool ensureAyahExists(int surah, int ayah);
void playbackTask(void *pvParameters);
void startSurahRepeat(int surah, int times);
bool playAyahRange(int surah, int startAyah, int endAyah, bool continueAfterRange);
bool playAyahCount(int surah, int startAyah, int count, bool continueAfterRange);
void stopCurrentForNewCommand();
bool playAyahRange(int surah, int startAyah, int endAyah, bool continueAfterRange);
bool playAyahCount(int surah, int startAyah, int count, bool continueAfterRange);
bool playAyahRange(int surah, int startAyah, int endAyah, bool continueAfterRange);
bool playAyahCount(int surah, int startAyah, int count, bool continueAfterRange);


void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUD_RATE);
  delay(300);
  DEBUG_SERIAL.println("[ESP32 Quran Player - MAX98357A]");

  pinMode(LED_PIN, OUTPUT);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // Try faster SD clock to reduce gaps; fall back if the card can't handle it.
  const uint32_t SD_FAST_HZ = 20000000;  // 20 MHz
  const uint32_t SD_SAFE_HZ = 3000000;   // 4 MHz
  if (!SD.begin(SD_CS, SPI, SD_FAST_HZ)) {
    DEBUG_SERIAL.println("SD init failed at 20 MHz, retrying at 4 MHz...");
    if (!SD.begin(SD_CS, SPI, SD_SAFE_HZ)) {
      DEBUG_SERIAL.println("SD init failed");
      while (1);
    }
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
    DEBUG_SERIAL.println("[WiFi] Initial attempt timed out, will keep retrying in loop.");
    wifiConnected = false;
  }


  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    DEBUG_SERIAL.println("[WiFi] Connected");
    DEBUG_SERIAL.print("[WiFi] IP: ");
    DEBUG_SERIAL.println(WiFi.localIP());
  } else {
    DEBUG_SERIAL.print("[WiFi] Failed with status ");
    DEBUG_SERIAL.println(WiFi.status());

    wifiConnected = false;
    DEBUG_SERIAL.println("[WiFi] Connection failed, will keep retrying in loop.");
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
      String mode = doc.containsKey("mode") ? doc["mode"].as<String>() : "continue";
      bool continueMode = mode.equalsIgnoreCase("continue");

      // Mirror serial command behavior: allow basmala when in continue mode
      stopCurrentForNewCommand();
      clearRangeLimits();
      if (!startAyahPlayback(surah, ayah, continueMode, continueMode)) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Ayah file not found\"}", true);
      }

      String response = "{\"success\":true,\"surah\":" + String(surah) + ",\"ayah\":" + String(ayah) + ",\"mode\":\"" + mode + "\"}";
      return WebSocketMCP::ToolResponse(response);
    }
  );

  // Play a range of ayahs within a surah; default mode is stop after range
  mcpClient.registerTool(
    "play_range",
    "Play from start ayah to end ayah (inclusive) within a surah",
    "{\"properties\":{\"surah\":{\"type\":\"integer\"},\"startAyah\":{\"type\":\"integer\"},\"endAyah\":{\"type\":\"integer\"},\"mode\":{\"type\":\"string\",\"enum\":[\"stop\",\"continue\"]}},\"required\":[\"surah\",\"startAyah\",\"endAyah\"],\"title\":\"playRangeArguments\",\"type\":\"object\"}",
    [](const String &args) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, args);
      if (error) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid JSON\"}", true);
      }
      int surah = doc["surah"];
      int startAyah = doc["startAyah"];
      int endAyah = doc["endAyah"];
      String mode = doc.containsKey("mode") ? doc["mode"].as<String>() : "stop";
      bool contAfter = mode.equalsIgnoreCase("continue");
      stopCurrentForNewCommand();
      clearRangeLimits();
      if (!playAyahRange(surah, startAyah, endAyah, contAfter)) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid range or files missing\"}", true);
      }
      String response = "{\"success\":true,\"surah\":" + String(surah) + ",\"startAyah\":" + String(startAyah) + ",\"endAyah\":" + String(endAyah) + ",\"mode\":\"" + mode + "\"}";
      return WebSocketMCP::ToolResponse(response);
    }
  );

  // Play N ayahs starting at a given ayah; default is stop after range
  mcpClient.registerTool(
    "play_after",
    "Play N ayahs starting at a specific ayah (inclusive)",
    "{\"properties\":{\"surah\":{\"type\":\"integer\"},\"startAyah\":{\"type\":\"integer\"},\"count\":{\"type\":\"integer\"},\"mode\":{\"type\":\"string\",\"enum\":[\"stop\",\"continue\"]}},\"required\":[\"surah\",\"startAyah\",\"count\"],\"title\":\"playAfterArguments\",\"type\":\"object\"}",
    [](const String &args) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, args);
      if (error) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid JSON\"}", true);
      }
      int surah = doc["surah"];
      int startAyah = doc["startAyah"];
      int count = doc["count"];
      String mode = doc.containsKey("mode") ? doc["mode"].as<String>() : "stop";
      bool contAfter = mode.equalsIgnoreCase("continue");
      stopCurrentForNewCommand();
      clearRangeLimits();
      if (!playAyahCount(surah, startAyah, count, contAfter)) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid range or files missing\"}", true);
      }
      String response = "{\"success\":true,\"surah\":" + String(surah) + ",\"startAyah\":" + String(startAyah) + ",\"count\":" + String(count) + ",\"mode\":\"" + mode + "\"}";
      return WebSocketMCP::ToolResponse(response);
    }
  );

    // Play an entire surah starting from its first available ayah and auto-advance afterward
  mcpClient.registerTool(
    "play_surah",
    "Play a whole surah (starts at the first available ayah and continues)",
    "{\"properties\":{\"surah\":{\"type\":\"integer\"}},\"required\":[\"surah\"],\"title\":\"playSurahArguments\",\"type\":\"object\"}",
    [](const String &args) {
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, args);
      if (error) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid JSON\"}", true);
      }
      int surah = doc["surah"];
      int firstAyah = findFirstAyahInSurah(surah);
      if (firstAyah == 0) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Surah files not found\"}", true);
      }
      if (!startAyahPlayback(surah, firstAyah, true)) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Failed to start playback\"}", true);
      }
      String response = "{\"success\":true,\"surah\":" + String(surah) + ",\"ayah\":" + String(firstAyah) + ",\"mode\":\"continue\"}";
      return WebSocketMCP::ToolResponse(response);
    }
  );
  mcpClient.registerTool(
    "repeat_surah",
    "Play a surah multiple times (continues through each run, then stops)",
    "{\"properties\":{\"surah\":{\"type\":\"integer\"},\"times\":{\"type\":\"integer\"}},\"required\":[\"surah\",\"times\"],\"title\":\"repeatSurahArguments\",\"type\":\"object\"}",
    [](const String &args) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, args);
      if (error) {
        return WebSocketMCP::ToolResponse("{\"success\":false,\"error\":\"Invalid JSON\"}", true);
      }
      int surah = doc["surah"];
      int times = doc["times"];
      if (times < 1) times = 1;
      startSurahRepeat(surah, times);
      return WebSocketMCP::ToolResponse("{\"success\":true,\"surah\":" + String(surah) + ",\"times\":" + String(times) + "}");
    }
  );
  mcpClient.registerTool(
    "stop_playback",
    "Stop Quran playback",
    "{\"properties\":{},\"title\":\"stopPlaybackArguments\",\"type\":\"object\"}",
    [](const String &args) {
      stopPlayback(true);
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
    } else if (index < (int)sizeof(buffer) - 1) {
      buffer[index++] = inChar;
      DEBUG_SERIAL.print(inChar);
    }
  }
}


void handleSerialCommand(const String &command) {
  if (command.length() == 0) return;
  String cmd = command; cmd.trim();

  if (cmd.equalsIgnoreCase("help")) { printHelp(); return; }
  if (cmd.equalsIgnoreCase("status")) { printStatus(); return; }
  if (cmd.equalsIgnoreCase("stop")) { stopPlayback(true, true); DEBUG_SERIAL.println("Playback stopped."); return; }
  if (cmd.equalsIgnoreCase("reconnect")) { DEBUG_SERIAL.println("Reconnecting MCP..."); mcpClient.disconnect(); mcpClient.begin(MCP_ENDPOINT, onMcpConnectionChange); return; }

  // play_ayah <surah> <ayah> [stop]
  if (cmd.startsWith("play_ayah ")) {
    stopCurrentForNewCommand();
    clearRangeLimits();
    String parts = cmd.substring(strlen("play_ayah ")); parts.trim();
    int s = -1, a = -1; String mode = "continue"; // default continue
    int sp = parts.indexOf(' ');
    if (sp >= 0) {
      s = parts.substring(0, sp).toInt();
      String rest = parts.substring(sp + 1); rest.trim();
      int sp2 = rest.indexOf(' ');
      if (sp2 >= 0) { a = rest.substring(0, sp2).toInt(); mode = rest.substring(sp2 + 1); }
      else { a = rest.toInt(); }
    }
    if (s <= 0 || a <= 0) { DEBUG_SERIAL.println("Usage: play_ayah <surah> <ayah> [stop]"); return; }
    bool cont = !mode.equalsIgnoreCase("stop");  // anything but "stop" continues
    if (!startAyahPlayback(s, a, cont, cont)) DEBUG_SERIAL.println("Ayah not found");
    else DEBUG_SERIAL.printf("Playing Ayah %03d:%03d (%s)\n", s, a, cont ? "continue" : "stop");
    return;
  }

  // play_range <surah> <startAyah> <endAyah> [continue]
  if (cmd.startsWith("play_range ")) {
    stopCurrentForNewCommand();
    String parts = cmd.substring(strlen("play_range ")); parts.trim();
    int sp1 = parts.indexOf(' ');
    int sp2 = (sp1 >= 0) ? parts.indexOf(' ', sp1 + 1) : -1;
    if (sp1 < 0 || sp2 < 0) { DEBUG_SERIAL.println("Usage: play_range <surah> <startAyah> <endAyah> [continue]"); return; }
    int s = parts.substring(0, sp1).toInt();
    int startA = parts.substring(sp1 + 1, sp2).toInt();
    String rest = parts.substring(sp2 + 1); rest.trim();
    int sp3 = rest.indexOf(' ');
    int endA = (sp3 >= 0) ? rest.substring(0, sp3).toInt() : rest.toInt();
    String mode = (sp3 >= 0) ? rest.substring(sp3 + 1) : "stop";
    bool contAfter = mode.equalsIgnoreCase("continue");
    if (!playAyahRange(s, startA, endA, contAfter)) DEBUG_SERIAL.println("Range invalid or files missing");
    else DEBUG_SERIAL.printf("Playing range %03d:%03d-%03d (%s after)\n", s, startA, endA, contAfter ? "continue" : "stop");
    return;
  }

  // play_after <surah> <startAyah> <count> [continue]
  if (cmd.startsWith("play_after ")) {
    stopCurrentForNewCommand();
    String parts = cmd.substring(strlen("play_after ")); parts.trim();
    int sp1 = parts.indexOf(' ');
    int sp2 = (sp1 >= 0) ? parts.indexOf(' ', sp1 + 1) : -1;
    if (sp1 < 0 || sp2 < 0) { DEBUG_SERIAL.println("Usage: play_after <surah> <startAyah> <count> [continue]"); return; }
    int s = parts.substring(0, sp1).toInt();
    int startA = parts.substring(sp1 + 1, sp2).toInt();
    String rest = parts.substring(sp2 + 1); rest.trim();
    int sp3 = rest.indexOf(' ');
    int count = (sp3 >= 0) ? rest.substring(0, sp3).toInt() : rest.toInt();
    String mode = (sp3 >= 0) ? rest.substring(sp3 + 1) : "stop";
    bool contAfter = mode.equalsIgnoreCase("continue");
    if (!playAyahCount(s, startA, count, contAfter)) DEBUG_SERIAL.println("Range invalid or files missing");
    else DEBUG_SERIAL.printf("Playing %d ayahs from %03d:%03d (%s after)\n", count, s, startA, contAfter ? "continue" : "stop");
    return;
  }

  // play_surah <surah>
  if (cmd.startsWith("play_surah ")) {
    stopCurrentForNewCommand();
    clearRangeLimits();
    int s = cmd.substring(strlen("play_surah ")).toInt();
    int first = findFirstAyahInSurah(s);
    if (first == 0) { DEBUG_SERIAL.println("Surah files not found"); return; }
    if (!startAyahPlayback(s, first, true, true)) DEBUG_SERIAL.println("Failed to start surah");
    else DEBUG_SERIAL.printf("Playing surah %03d from ayah %03d\n", s, first);
    return;
  }

  // repeat_surah <surah> <times>
  if (cmd.startsWith("repeat_surah ")) {
    stopCurrentForNewCommand();
    clearRangeLimits();
    String parts = cmd.substring(strlen("repeat_surah ")); parts.trim();
    int sp = parts.indexOf(' ');
    int s = (sp >= 0) ? parts.substring(0, sp).toInt() : parts.toInt();
    int t = (sp >= 0) ? parts.substring(sp + 1).toInt() : 1;
    if (s <= 0) { DEBUG_SERIAL.println("Usage: repeat_surah <surah> <times>"); return; }
    if (t < 1) t = 1;
    startSurahRepeat(s, t);
    DEBUG_SERIAL.printf("Repeating surah %03d (%d times)\n", s, t);
    return;
  }

  DEBUG_SERIAL.println("Unknown command. Type \"help\" for usage.");
}

void printHelp() {
  DEBUG_SERIAL.println("Available commands:");
  DEBUG_SERIAL.println("  help");
  DEBUG_SERIAL.println("  status");
  DEBUG_SERIAL.println("  stop");
  DEBUG_SERIAL.println("  reconnect");
  DEBUG_SERIAL.println("  play_ayah <surah> <ayah> [stop]");
  DEBUG_SERIAL.println("  play_range <surah> <startAyah> <endAyah> [continue]");
  DEBUG_SERIAL.println("  play_after <surah> <startAyah> <count> [continue]");
  DEBUG_SERIAL.println("  play_surah <surah>");
  DEBUG_SERIAL.println("  repeat_surah <surah> <times>");

}


void printStatus() {
  DEBUG_SERIAL.println("Status:");
  DEBUG_SERIAL.printf("  WiFi: %s`n", wifiConnected ? "connected" : "disconnected");
  DEBUG_SERIAL.printf("  MCP: %s`n", mcpConnected ? "connected" : "disconnected");
  if (playback.active) {
    DEBUG_SERIAL.printf("  Playing Ayah %03d:%03d (mode: %s)`n", playback.surah, playback.ayah, playback.autoAdvance ? "continue" : "stop");
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
  // DEBUG_SERIAL.printf("[Quran] Checking %s => %s`n", path.c_str(), found ? "found" : "missing");
  return found;
}

void startSurahRepeat(int surah, int times) {
  int firstAyah = findFirstAyahInSurah(surah);
  if (firstAyah == 0) return;
  repeatSurahMode = true;
  repeatSurahId = surah;
  repeatSurahRemaining = times -1;  // first play is already started
  startAyahPlayback(surah, firstAyah, true);
}

// Stop any current playback cleanly before starting a new command
void stopCurrentForNewCommand() {
  stopPlayback(true, true);   // reset repeat/basmala/range
}

// Play a range within a surah, optionally continuing after the range ends
bool playAyahRange(int surah, int startAyah, int endAyah, bool continueAfterRange) {
  if (startAyah <= 0 || endAyah <= 0 || endAyah < startAyah) return false;
  playback.rangeActive = true;
  playback.rangeEndAyah = endAyah;
  playback.rangeCountRemaining = 0;
  playback.rangeContinueAfter = continueAfterRange;
  return startAyahPlayback(surah, startAyah, true, true);
}

// Play N ayahs starting at startAyah (inclusive)
bool playAyahCount(int surah, int startAyah, int count, bool continueAfterRange) {
  if (startAyah <= 0 || count <= 0) return false;
  playback.rangeActive = true;
  playback.rangeEndAyah = startAyah + count - 1;
  playback.rangeCountRemaining = count - 1; // we will decrement as we advance
  playback.rangeContinueAfter = continueAfterRange;
  return startAyahPlayback(surah, startAyah, true, true);
}

bool startAyahPlayback(int surah, int ayah, bool continueMode, bool allowBasmala) {
  // Inject basmala before the first ayah of every surah except 9, using the shared file 001/001.mp3
  if (allowBasmala && !playback.playingBasmala && ayah == 1 && surah != BASMALA_SURAH && surah != 9) {
    playback.basmalaPending = true;
    playback.pendingSurah = surah;
    playback.pendingAyah = ayah;
    playback.pendingAutoAdvance = continueMode;
    playback.playingBasmala = true;
    // Play basmala once, then resume the requested surah/ayah
    surah = BASMALA_SURAH;
    ayah = BASMALA_AYAH;
    continueMode = false;
  } else {
    if (!playback.playingBasmala) {
      playback.basmalaPending = false;
      playback.pendingSurah = 0;
      playback.pendingAyah = 0;
      playback.pendingAutoAdvance = false;
    }
  }

  if (!ensureAyahExists(surah, ayah)) {
    return false;
  }

  // release previous decoder/file before opening a new one
  if (mp3->isRunning()) mp3->stop();
  if (audioFile) { delete audioFile; audioFile = nullptr; }

  String path = buildAyahPath(surah, ayah);
  audioFile = new AudioFileSourceSD(path.c_str());

  mp3->begin(audioFile, out);

  playback.surah = surah;
  playback.ayah = ayah;
  playback.autoAdvance = continueMode;
  playback.waitingForNextSurah = false;
  playback.active = true;
  playback.waitUntil = 0;
  // do not clear range flags here; they are controlled by caller

  return true;
}

void stopPlayback(bool resetRepeat, bool resetBasmala) {
  playback.active = false;
  playback.waitingForNextSurah = false;
  playback.waitUntil = 0;
  playback.autoAdvance = false;
  clearRangeLimits();
  if (resetBasmala) {
    playback.basmalaPending = false;
    playback.pendingSurah = 0;
    playback.pendingAyah = 0;
    playback.pendingAutoAdvance = false;
    playback.playingBasmala = false;
  }
  if (resetRepeat) {
    repeatSurahMode = false;
    repeatSurahRemaining = 0;
    repeatSurahId = 0;
  }
  if (mp3->isRunning()) mp3->stop();
  if (audioFile) { delete audioFile; audioFile = nullptr; }
}

void handlePlaybackCompletion() {
  if (!playback.active || mp3->isRunning()) return;

  // If we've already queued the next surah, don't reschedule on every loop
  // iteration. The audio task runs continuously after playback ends, and
  // without this guard we keep resetting the timer and spamming the log,
  // preventing the advance from ever firing.
  if (playback.waitingForNextSurah) return;

  if (playback.playingBasmala && playback.basmalaPending) {
    playback.playingBasmala = false;
    int targetSurah = playback.pendingSurah;
    int targetAyah = playback.pendingAyah;
    bool targetAuto = playback.pendingAutoAdvance;
    playback.basmalaPending = false;
    playback.pendingSurah = 0;
    playback.pendingAyah = 0;
    playback.pendingAutoAdvance = false;
    startAyahPlayback(targetSurah, targetAyah, targetAuto, false);
    return;
  }

  if (playback.autoAdvance) {
    // Respect range limits if active
    if (playback.rangeActive) {
      if (playback.rangeCountRemaining > 0) {
        playback.rangeCountRemaining--;
      } else if (playback.ayah >= playback.rangeEndAyah && !playback.rangeContinueAfter) {
        // End of range reached; stop auto-advance
        playback.rangeActive = false;
        playback.autoAdvance = false;
        playback.active = false;
        if (audioFile) { delete audioFile; audioFile = nullptr; }
        return;
      }
    }

    int nextAyah = playback.ayah + 1;
    if (startAyahPlayback(playback.surah, nextAyah, true)) {
      return;
    }

    playback.waitingForNextSurah = true;
    playback.waitUntil = millis() + surahEndDelayMs;
    DEBUG_SERIAL.println("[advance] end of surah, waiting to advance");

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

  // Handle surah repeats first
  if (repeatSurahMode) {
    DEBUG_SERIAL.printf("[repeat] pending=%d surah=%d\n", repeatSurahRemaining, repeatSurahId);
    if (repeatSurahRemaining > 0) {
      repeatSurahRemaining--;
      int firstAyah = findFirstAyahInSurah(repeatSurahId);
      DEBUG_SERIAL.printf("[repeat] restarting surah %03d from ayah %03d, remaining=%d\n",
                          repeatSurahId, firstAyah, repeatSurahRemaining);
      if (firstAyah > 0) {
        startAyahPlayback(repeatSurahId, firstAyah, true);
        return;
      }
    }
    DEBUG_SERIAL.println("[repeat] repeats finished or missing files; exiting repeat mode");
    repeatSurahMode = false;
    repeatSurahId = 0;
    repeatSurahRemaining = 0;
    playback.autoAdvance = false;       // stop advancing to next surah
    playback.waitingForNextSurah = false;
      DEBUG_SERIAL.printf("[update] repeatMode=%d remaining=%d autoAdvance=%d\n", repeatSurahMode, repeatSurahRemaining, playback.autoAdvance);

    playback.active = false;
    return;
  }



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
