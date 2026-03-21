# ESP32 Quran MCP Client

Offline-friendly Quran audio player for ESP32 with MAX98357A I2S amp, SD card storage, MCP control, and IR remote (HX1838) input.

## Hardware / Wiring
- SD card (SPI): CS `5`, SCK `18`, MISO `4`, MOSI `23`
- MAX98357A (I2S out): BCLK `14`, LRC `27`, DIN `33`
- Status LED: `2`
- Config button (hold ~7s to enter AP mode): `22`
- IR receiver: `21` (OK = play/retry current surah selection, STAR = stop)

## Libraries
- ESP32 core (for `SD`, `SPI`, `WiFi`)
- ESP8266Audio
- ArduinoJson

Install via Arduino CLI:
```
arduino-cli core update-index
arduino-cli lib update-index
arduino-cli lib install "ESP8266Audio" "ArduinoJson"
```
Or run `scripts/install-arduino-libraries.ps1`.

## Controls
- Serial/MCP commands: `play stop <surah> <ayah>`, `play continue <surah> <ayah>`, `stop`, `status`, `help`, `play_surah`, `repeat_surah`, `play_ayah`.
- IR remote (HX1838 codes hardcoded):
  - STAR: stop playback
  - DIGITS then OK: play surah/ayah (ayah defaults to 1 if unset)
  - OK with no digits: stop current playback
  - UP/DOWN: browse surahs; LEFT: backspace
  - HASH x7: enter Wi‑Fi config AP (same as holding button on pin 22)

## Notes
- SD clock: tries 20 MHz, falls back to 4 MHz.
- Audio runs on core 1; IR/polling and Wi‑Fi on core 0.
- Device continues playback without internet; MCP is optional.
