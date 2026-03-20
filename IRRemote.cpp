#include "IRRemote.h"
#include <Arduino.h>
#include "IRController.h"
#include "InputEngine.h"

// Dedicated IR handler living with the main project, no extra sketch needed.
static IRController ir(21);  // ESP32 GPIO 21
static InputEngine engine;

// Callbacks provided by the main sketch
extern bool startAyahPlayback(int surah, int ayah, bool continueMode, bool allowBasmala);
extern void stopPlayback(bool resetRepeat, bool resetBasmala);

static void playAyah(int surah, int ayah) {
  startAyahPlayback(surah, ayah, true, true);
}

static void stopAll() {
  stopPlayback(true, true);
}

void irRemoteBegin() {
  ir.begin();
  engine.setCallbacks(playAyah, stopAll);
}

void irRemoteLoop() {
  RemoteButton btn;
  uint32_t raw;

  if (!ir.getButton(btn, raw)) return;

  Serial.print("HEX: 0x");
  Serial.println(raw, HEX);

  engine.handle(btn);
}
