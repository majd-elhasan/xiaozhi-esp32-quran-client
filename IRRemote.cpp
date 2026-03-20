#include "IRRemote.h"
#include <Arduino.h>
#include "IRController.h"
#include "InputEngine.h"

// Dedicated IR handler living with the main project, no extra sketch needed.
static IRController ir(21);  // ESP32 GPIO 21
static InputEngine engine;

void irRemoteBegin() {
  ir.begin();
}

void irRemoteLoop() {
  RemoteButton btn;
  uint32_t raw;

  if (!ir.getButton(btn, raw)) return;

  Serial.print("HEX: 0x");
  Serial.println(raw, HEX);

  engine.handle(btn);
}
