#include "IRController.h"

IRController::IRController(uint8_t pin) : decoder(pin) {}

void IRController::begin() {
    decoder.begin();
}

RemoteButton IRController::decode(uint32_t code) {
    switch(code) {
        case 0xFFA25D: return BTN_1;
        case 0xFF629D: return BTN_2;
        case 0xFFE21D: return BTN_3;
        case 0xFF22DD: return BTN_4;
        case 0xFF02FD: return BTN_5;
        case 0xFFC23D: return BTN_6;
        case 0xFFE01F: return BTN_7;
        case 0xFFA857: return BTN_8;
        case 0xFF906F: return BTN_9;
        case 0xFF9867: return BTN_0;
        case 0xFF6897: return BTN_STAR;
        case 0xFFB04F: return BTN_HASH;
        case 0xFF18E7: return BTN_UP;
        case 0xFF4AB5: return BTN_DOWN;
        case 0xFF10EF: return BTN_LEFT;
        case 0xFF5AA5: return BTN_RIGHT;
        case 0xFF38C7: return BTN_OK;
        default: return BTN_UNKNOWN;
    }
}

bool IRController::getButton(RemoteButton &btn, uint32_t &raw) {

    if (millis() - lastSignal > RELEASE_TIMEOUT) {
        locked = false;
    }

    if (!decoder.available()) return false;

    uint32_t code = decoder.getDecodedData();

    if ((code & 0xFF0000) != 0xFF0000) return false;

    lastSignal = millis();

    if (locked) return false;

    locked = true;

    raw = code;
    btn = decode(code);

    return true;
}