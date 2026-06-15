#pragma once

// USB serial command console (e/d/st/msr/msl/cb/pp/nx/pv + @INJ/@EMU). Extracted
// verbatim from main.cpp. Drives the main-side globals `display`, `btMode`,
// `gotFrame` (declared extern in the .cpp). The WebSocket @KEY/@INJ/@EMU path
// (wireCommand) stays in main — it is a different transport (WireLink), not serial.
namespace SerialConsole {
    void begin();   // Serial.begin + banner + register commands (call in setup)
    void loop();    // pump serial input (call from loop)
}
