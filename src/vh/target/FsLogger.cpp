#include "FsLogger.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

namespace VhFs
{
    static constexpr size_t PSRAM_CAP = 256 * 1024;   // minutes of full-bus text
    static constexpr size_t HEAP_CAP  = 16 * 1024;    // fallback without PSRAM
    static constexpr size_t FLUSH_CHUNK = 4096;

    bool FsLogger::begin()
    {
        _mounted = LittleFS.begin(/*formatOnFail=*/true);
        if (!_mounted)
        {
            Serial.println("[fs] LittleFS mount FAILED");
            return false;
        }
        if (psramFound())
        {
            _buf = (char*)ps_malloc(PSRAM_CAP);
            _cap = _buf ? PSRAM_CAP : 0;
        }
        if (!_buf)
        {
            _buf = (char*)malloc(HEAP_CAP);
            _cap = _buf ? HEAP_CAP : 0;
        }
        Serial.printf("[fs] mounted, capture buffer %u bytes (%s)\n",
                      (unsigned)_cap, psramFound() ? "PSRAM" : "heap");
        return _cap > 0;
    }

    bool FsLogger::start(uint8_t mode, uint16_t seconds, uint32_t nowMs)
    {
        if (!_mounted || !_buf) return false;
        if (_active) stop();

        // Rotate: overwrite the oldest of /cap0..capN by write time; simplest
        // robust pick — the slot after the newest existing one.
        int newest = -1;
        time_t newestT = 0;
        for (int i = 0; i < ROTATE_FILES; i++)
        {
            char n[24];
            snprintf(n, sizeof(n), "/cap%d.canlog", i);
            File f = LittleFS.open(n, "r");
            if (!f) { newest = (i == 0) ? newest : newest; continue; }
            time_t t = f.getLastWrite();
            if (t >= newestT) { newestT = t; newest = i; }
            f.close();
        }
        int slot = (newest + 1) % ROTATE_FILES;
        snprintf(_curName, sizeof(_curName), "/cap%d.canlog", slot);
        LittleFS.remove(_curName);

        _mode = mode;
        _active = true;
        _startMs = nowMs;
        _stopAtMs = seconds ? nowMs + (uint32_t)seconds * 1000 : 0;   // 0 = manual stop
        _len = 0;
        _flushedTo = 0;

        int n = snprintf(_buf, _cap,
                         "# MeganeCAN .canlog v1 - VH capture mode=%u t0=%lu\n"
                         "# format: <ms> @RX <id-hex> <bytes...> | <ms> @HU <cmd-hex> <bytes...>\n",
                         mode, (unsigned long)nowMs);
        _len = (n > 0) ? (size_t)n : 0;
        Serial.printf("[cap] start %s mode=%u secs=%u\n", _curName, mode, seconds);
        return true;
    }

    void FsLogger::emitLine(const char* tag, uint16_t id, const uint8_t* data,
                            uint8_t len, uint32_t nowMs)
    {
        // "<ms> @RX 354 13 88 00 ..." — ms relative to capture start, like the
        // fixture files.
        char line[64];
        int p = snprintf(line, sizeof(line), "%lu %s %X",
                         (unsigned long)(nowMs - _startMs), tag, id);
        for (uint8_t i = 0; i < len && p < (int)sizeof(line) - 4; i++)
            p += snprintf(line + p, sizeof(line) - p, " %02X", data[i]);
        line[p++] = '\n';

        if (_len + p >= _cap) { _dropped++; return; }   // buffer full
        memcpy(_buf + _len, line, p);
        _len += p;
    }

    void FsLogger::onCanFrame(uint16_t id, const uint8_t* data, uint8_t dlc,
                              bool knownId, uint32_t nowMs)
    {
        if (!_active) return;
        if (_mode == CAP_HU) return;
        if (_mode == CAP_UNKNOWN && knownId) return;
        emitLine("@RX", id, data, dlc, nowMs);
    }

    void FsLogger::onHuFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                             uint32_t nowMs)
    {
        if (!_active) return;
        if (_mode == CAP_UNKNOWN) return;
        emitLine("@HU", cmd, payload, len, nowMs);
    }

    void FsLogger::flush(bool force)
    {
        if (_flushedTo >= _len) return;
        size_t pending = _len - _flushedTo;
        if (!force && pending < FLUSH_CHUNK) return;

        File f = LittleFS.open(_curName, _flushedTo ? "a" : "w");
        if (!f) { _dropped++; return; }
        f.write((const uint8_t*)_buf + _flushedTo, pending);
        f.close();
        _flushedTo = _len;
    }

    void FsLogger::service(uint32_t nowMs)
    {
        if (!_active) return;
        if (_stopAtMs && nowMs >= _stopAtMs)
        {
            Serial.printf("[cap] auto-stop, %u bytes, %u dropped\n",
                          (unsigned)_len, (unsigned)_dropped);
            stop();
            return;
        }
        if (nowMs - _lastFlushMs >= 1000)
        {
            _lastFlushMs = nowMs;
            flush(false);
        }
    }

    void FsLogger::stop()
    {
        if (!_active) return;
        _active = false;
        flush(true);
    }

    uint8_t FsLogger::ls(Entry* out, uint8_t maxN)
    {
        uint8_t n = 0;
        if (!_mounted) return 0;
        File root = LittleFS.open("/");
        if (!root) return 0;
        File f;
        while (n < maxN && (f = root.openNextFile()))
        {
            snprintf(out[n].name, sizeof(out[n].name), "/%s", f.name());
            out[n].size = f.size();
            n++;
            f.close();
        }
        root.close();
        return n;
    }

    int FsLogger::readFile(const char* name, uint32_t offset, uint8_t* buf,
                           uint32_t maxLen, uint32_t& totalSize)
    {
        totalSize = 0;
        if (!_mounted) return -1;
        File f = LittleFS.open(name, "r");
        if (!f) return -1;
        totalSize = f.size();
        if (offset >= totalSize) { f.close(); return 0; }
        f.seek(offset);
        int n = f.read(buf, maxLen);
        f.close();
        return n < 0 ? 0 : n;
    }
}
