#include "IRController.h"
#include "InputEngine.h"

IRController ir(21);  // esp32 gpio 21
InputEngine engine;

void setup() {
    Serial.begin(115200);
    ir.begin();
}

void loop() {
    RemoteButton btn;
    uint32_t raw;

    if (ir.getButton(btn, raw)) {
        Serial.print("HEX: 0x");
        Serial.println(raw, HEX);

        engine.handle(btn);
    }
}