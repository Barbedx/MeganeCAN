#include "CanLog.h"

#include <Preferences.h>
#include <set>
#include <map>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace CanLog
{
    namespace
    {
        const size_t      CAP = 16384;
        char              rb[CAP];
        size_t            head = 0;
        bool              wrapped = false;

        bool                          gEnabled = false;
        std::set<uint32_t>            gIds;  // allow-list; empty => all
        std::map<uint32_t, uint32_t>  gSeen; // id -> count
        SemaphoreHandle_t             mtx = nullptr;

        void ensure() { if (!mtx) mtx = xSemaphoreCreateMutex(); }
        inline void put(char c) { rb[head++] = c; if (head >= CAP) { head = 0; wrapped = true; } }
        void appendStr(const char *s) { while (*s) put(*s++); }

        String idsToCsv()
        {
            String s; bool first = true;
            for (uint32_t id : gIds) { if (!first) s += ","; first = false; char b[10]; snprintf(b, sizeof(b), "%X", (unsigned)id); s += b; }
            return s;
        }

        void parseCsv(const String &csv)
        {
            gIds.clear();
            int i = 0, len = csv.length();
            while (i < len)
            {
                while (i < len && !isxdigit((unsigned char)csv[i])) i++;
                int j = i;
                while (j < len && isxdigit((unsigned char)csv[j])) j++;
                if (j > i) gIds.insert((uint32_t)strtol(csv.substring(i, j).c_str(), nullptr, 16));
                i = j + 1;
            }
        }
    } // namespace

    void begin()
    {
        ensure();
        Preferences p;
        p.begin("canlog", true);
        gEnabled = p.getBool("en", false);
        String ids = p.getString("ids", "");
        p.end();
        parseCsv(ids);
    }

    void onFrame(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data)
    {
        (void)ext;
        ensure();
        // Short timeout: never stall the CAN callback under high bus load.
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(4)) != pdTRUE)
            return;
        gSeen[id]++;
        if (gEnabled && (gIds.empty() || gIds.count(id)))
        {
            char line[48];
            snprintf(line, sizeof(line), "[%lu] %03X [%u]", (unsigned long)millis(), (unsigned)id, (unsigned)dlc);
            appendStr(line);
            for (uint8_t i = 0; i < dlc && i < 8; i++) { char b[5]; snprintf(b, sizeof(b), " %02X", data[i]); appendStr(b); }
            put('\n');
        }
        xSemaphoreGive(mtx);
    }

    String dump()
    {
        ensure();
        String s;
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            s.reserve((wrapped ? CAP : head) + 1);
            if (wrapped) for (size_t i = head; i < CAP; ++i) s += rb[i];
            for (size_t i = 0; i < head; ++i) s += rb[i];
            xSemaphoreGive(mtx);
        }
        return s;
    }

    void clear()
    {
        ensure();
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            head = 0; wrapped = false; gSeen.clear();
            xSemaphoreGive(mtx);
        }
    }

    void setConfig(bool enabled, const String &idsCsv)
    {
        ensure();
        String norm;
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            gEnabled = enabled;
            parseCsv(idsCsv);
            norm = idsToCsv();
            xSemaphoreGive(mtx);
        }
        Preferences p;
        p.begin("canlog", false);
        p.putBool("en", enabled);
        p.putString("ids", norm);
        p.end();
    }

    String configJson()
    {
        ensure();
        String j = "{";
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            j += "\"enabled\":"; j += gEnabled ? "true" : "false";
            j += ",\"filter\":\"" + idsToCsv() + "\",\"seen\":[";
            bool first = true;
            for (auto &kv : gSeen) { if (!first) j += ","; first = false; char b[48]; snprintf(b, sizeof(b), "{\"id\":\"%03X\",\"n\":%u}", (unsigned)kv.first, (unsigned)kv.second); j += b; }
            j += "]";
            xSemaphoreGive(mtx);
        }
        j += "}";
        return j;
    }
}
