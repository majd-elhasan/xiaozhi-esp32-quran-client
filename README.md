# ESP32 Quran MCP Client

This sketch streams an MP3 stored on an SD card over the MAX98357A IÂ²S amplifier. It runs on the ESP32 core, so the `SD` and `SPI` headers are provided by `esp32:esp32`.

## Arduino-style library installation

### 1. Arduino CLI (preferred for repeatable builds)
1. Install [Arduino CLI](https://arduino.cc/learn/arduino-cli) and make sure `arduino-cli` is on your `PATH`.
2. Run `arduino-cli core update-index` (or `arduino-cli core update-index --additional-urls ...` if you rely on extra package URLs).
3. Run `arduino-cli lib update-index` to refresh the library database.
4. Run `arduino-cli lib install "ESP8266Audio"` and `arduino-cli lib install "ArduinoJson"` to fetch the audio and JSON dependencies. The CLI caches the downloads in your sketchbooklibraries folder.
5. If you prefer, run the helper script here to encapsulate those steps: `.\scripts/install-arduino-libraries.ps1`.

### 2. Arduino IDE
1. Open the imported sketch (`esp32_quran_mcp_client.ino`).
2. Choose **Sketch > Include Library > Manage Libraries...**.
3. Search for **ESP8266Audio** and **ArduinoJson**, then click **Install** on each.
4. The `SD` and `SPI` headers are already bundled with the ESP32 core (no extra install needed).

### 3. Manual download
1. Download [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) and [ArduinoJson](https://github.com/bblanchon/ArduinoJson) as ZIPs.
2. Place them into your `Documents/Arduino/libraries` (Arduino IDE) or `~/Arduino/libraries` (CLI sketchbook).
3. Restart the IDE/CLI so the libraries are registered.

## Helper script

`.\scripts/install-arduino-libraries.ps1` wraps the CLI commands above so you can run them from this repo instead of typing each one manually. It now installs both `ESP8266Audio` and `ArduinoJson` and continues to remind you if `arduino-cli` is missing.

## Quran playback and MCP commands

The updated sketch acts as a Quran player that can be driven over MCP or via the serial monitor:

* **Serial commands** (enter them into the Sketch Serial Monitor):
  * `play stop <surah> <ayah>` â€” play the requested ayah and stop when it ends.
  * `play continue <surah> <ayah>` â€” play from that ayah through the end of the surah, wait `surahEndDelayMs` (default 10â€¯s), and then automatically begin the next surah.
  * `stop` â€” stop whatever is currently playing.
  * `status`, `help`, and `reconnect` remain available exactly as before.
  * The console prints helpful output (including missing files) so you can verify path syntax before asking the AI to run a surah.
* **MCP tools** (exposed once the sketch connects to `MCP_ENDPOINT`):
  * `play_ayah` â€” accepts `surah`, `ayah`, and an optional `mode` (`stop` or `continue`) and triggers the same playback flows as the serial helper.
  * `stop_playback` â€” immediately cancels playback.

Because the CLI now compiles locally and the extension knows where `arduino-cli` lives, you can continue using the Verify/Upload buttons once VS Code is restarted. The helper script keeps the required JSON dependency installed alongside `ESP8266Audio`.
## MCP tools (AI commands)
- play_ayah: { surah, ayah, mode: stop|continue } – default stop.
- play_continue: { surah, ayah } – shorthand that always continues through the surah and auto-advances.
- stop_playback: {} – stop immediately.

These mirror the serial commands (play stop, play continue, stop, status, help). If you add new serial commands later, consider exposing them as MCP tools the same way: add another mcpClient.registerTool in egisterMcpTools() with a JSON schema and a lambda that calls your handler.
- play_surah: { surah } – starts at the first available ayah of the surah, continues to the end, then auto-advances.
- repeat_surah: { surah, times } – repeats the entire surah 'times' times in continue mode, then stops.\n
