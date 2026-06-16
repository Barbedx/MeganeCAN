#pragma once

// PC->fw command handler for the WireProto transport (today the WebSocket): @INJ / @EMU
// / @KEY. Register with WireProto::onCommand(WireConsole::handle, nullptr). Parallel to
// SerialConsole but a different transport (a WireLink line, not the SerialCommands lib).
// Drives the main-side globals `display` + `gotFrame` (extern in the .cpp).
namespace WireConsole {
    void handle(const char* line, void* ctx);
}
