#ifndef INPUT_ENGINE_H
#define INPUT_ENGINE_H

#include "IRController.h"

typedef void (*PlayCallback)(int surah, int ayah);
typedef void (*StopCallback)();
typedef void (*ApModeCallback)();

class InputEngine {
public:
    void handle(RemoteButton btn);
    void setCallbacks(PlayCallback play, StopCallback stop);
    void setApCallback(ApModeCallback ap);

private:
    enum State {
        IDLE,
        ENTER_SURAH,
        ENTER_AYAH
    };

    State state = IDLE;

    int surah = 0;
    int ayah = 0;

    int lastSelectSurah = 1;

    bool surahSet = false;
    bool ayahSet = false;

    // hash multi-press
    uint8_t hashCount = 0;
    unsigned long lastHash = 0;

    PlayCallback playCb = nullptr;
    StopCallback stopCb = nullptr;
    ApModeCallback apCb = nullptr;

    void reset();
    void addDigit(int d);
    void backspace();
    void printStatus();
};

#endif
