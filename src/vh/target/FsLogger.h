#pragma once
#include <stdint.h>
#include <stddef.h>

// Capture logger (ARCHITECTURE-V2 §5): PSRAM staging buffer -> LittleFS
// rotating .canlog files, same text format as tools/*.canlog so the existing
// tooling replays them:
//   <ms> @RX <id-hex> <bytes...>     vehicle-CAN frame
//   <ms> @HU <cmd-hex> <bytes...>    HU->box UART frame (payload bytes)
// Triggered captures: "everything for N seconds", "unknown IDs only", "HU UART".
namespace VhFs
{
    enum CapMode : uint8_t {
        CAP_ALL     = 0,   // every vehicle-CAN frame + HU frames
        CAP_UNKNOWN = 1,   // only IDs the decode table doesn't know
        CAP_HU      = 2,   // only HU->box UART traffic
    };

    class FsLogger {
    public:
        bool begin();                                   // mount LittleFS
        bool start(uint8_t mode, uint16_t seconds, uint32_t nowMs);
        void stop();
        bool active() const { return _active; }
        uint8_t mode() const { return _mode; }

        void onCanFrame(uint16_t id, const uint8_t* data, uint8_t dlc,
                        bool knownId, uint32_t nowMs);
        void onHuFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                       uint32_t nowMs);

        void service(uint32_t nowMs);                   // flush + auto-stop

        // File access for the FILE_* tunnel. ls() fills up to maxN entries.
        struct Entry { char name[24]; uint32_t size; };
        uint8_t ls(Entry* out, uint8_t maxN);
        // Read `maxLen` bytes at `offset`; returns bytes read (<0 = no file),
        // and reports the file's total size.
        int readFile(const char* name, uint32_t offset, uint8_t* buf,
                     uint32_t maxLen, uint32_t& totalSize);

        uint32_t droppedLines() const { return _dropped; }

        static constexpr uint8_t ROTATE_FILES = 4;      // /cap0.canlog .. /cap3

    private:
        void emitLine(const char* tag, uint16_t id, const uint8_t* data,
                      uint8_t len, uint32_t nowMs);
        void flush(bool force);

        bool _mounted = false;
        bool _active = false;
        uint8_t _mode = CAP_ALL;
        uint32_t _stopAtMs = 0;
        uint32_t _startMs = 0;
        char _curName[24] = {};

        // Staging buffer: PSRAM when available (minutes of full-bus traffic),
        // small heap fallback otherwise. Flushed to flash in chunks.
        char* _buf = nullptr;
        size_t _cap = 0;
        size_t _len = 0;
        size_t _flushedTo = 0;
        uint32_t _lastFlushMs = 0;
        uint32_t _dropped = 0;
    };
}
