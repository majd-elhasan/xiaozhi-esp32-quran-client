#include "InputEngine.h"
#include <Arduino.h>

void InputEngine::reset() {
    state = IDLE;
    lastSelectSurah = surah;
    surah = 0;
    ayah = 0;
    surahSet = false;
    ayahSet = false;
}

void InputEngine::addDigit(int d) {

    if (state == ENTER_SURAH) {
        int temp = surah * 10 + d;
        if (temp <= 114) {
            surah = temp;
            surahSet = true;
        }
    }
    else if (state == ENTER_AYAH) {
        int temp = ayah * 10 + d;
        if (temp <= 286) {
            ayah = temp;
            ayahSet = true;
        }
    }
}

void InputEngine::backspace() {
    if (state == ENTER_SURAH && surahSet) {
        surah /= 10;
        if (surah == 0) surahSet = false;
    }
    else if (state == ENTER_AYAH && ayahSet) {
        ayah /= 10;
        if (ayah == 0) ayahSet = false;
    }
}

void InputEngine::handle(RemoteButton btn) {

    Serial.print("BTN: ");
    Serial.println((int)btn);

    // ---------- HASH MULTI PRESS ----------
    if (btn == BTN_HASH) {
        if (millis() - lastHash > 1500) hashCount = 0;

        hashCount++;
        lastHash = millis();

        if (hashCount >= 7) {
            Serial.println(">>> AP MODE <<<");
            hashCount = 0;
            return;
        }

        if (surahSet) {
            state = ENTER_AYAH;
            Serial.println("Enter Ayah");
        }
        return;
    }

    hashCount = 0;

    // ---------- STAR ----------
    if (btn == BTN_STAR) {
        reset();
        state = ENTER_SURAH;
        Serial.println("Enter Surah");
        return;
    }

    // ---------- DIGITS ----------
    if (btn <= BTN_9) {
        addDigit(btn);
        printStatus();
        return;
    }

    // ---------- LEFT ----------
    if (btn == BTN_LEFT) {
        backspace();
        printStatus();
        return;
    }

    // ---------- UP / DOWN ----------
    if (btn == BTN_UP || btn == BTN_DOWN) {

        int s = surahSet ? surah : lastSelectSurah;

        if (btn == BTN_UP && s > 1) s--;
        if (btn == BTN_DOWN && s < 114) s++;

        surah = s;
        surahSet = true;
        state = ENTER_SURAH;

        printStatus();
        return;
    }

    // ---------- OK ----------
    if (btn == BTN_OK) {
        if (surahSet) {
            int a = ayahSet ? ayah : 1;

            Serial.print("PLAY S:");
            Serial.print(surah);
            Serial.print(" A:");
            Serial.println(a);
        }
        reset();
    }
}

void InputEngine::printStatus() {
    Serial.print("S:");
    Serial.print(surah);
    Serial.print(" A:");
    Serial.println(ayah);
}