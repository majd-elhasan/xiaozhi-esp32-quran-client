#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include "HX1838Decoder.h"

enum RemoteButton {
    BTN_0, BTN_1, BTN_2, BTN_3, BTN_4,
    BTN_5, BTN_6, BTN_7, BTN_8, BTN_9,
    BTN_STAR, BTN_HASH,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_OK,
    BTN_UNKNOWN
};

class IRController {
public:
    IRController(uint8_t pin);
    void begin();

    bool getButton(RemoteButton &btn, uint32_t &raw);

private:
    IRDecoder decoder;

    bool locked = false;
    unsigned long lastSignal = 0;

    const unsigned long RELEASE_TIMEOUT = 120;

    RemoteButton decode(uint32_t code);
};

#endif