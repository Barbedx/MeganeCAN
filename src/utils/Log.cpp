#include "Log.h"

#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace Log
{
    namespace
    {
        const size_t      CAP  = 16384;
        char              rb[CAP];
        size_t            head = 0;
        bool              wrapped = false;
        SemaphoreHandle_t mtx = nullptr;

        void ensure() { if (!mtx) mtx = xSemaphoreCreateMutex(); }
        inline void put(char c) { rb[head++] = c; if (head >= CAP) { head = 0; wrapped = true; } }
    }

    void printf(const char *fmt, ...)
    {
        char line[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);

        char out[288];
        snprintf(out, sizeof(out), "[%lu] %s\n", (unsigned long)millis(), line);
        Serial.print(out); // mirror to serial

        ensure();
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            for (char *p = out; *p; ++p) put(*p);
            xSemaphoreGive(mtx);
        }
    }

    String dump()
    {
        ensure();
        String s;
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            s.reserve((wrapped ? CAP : head) + 1);
            if (wrapped)
                for (size_t i = head; i < CAP; ++i) s += rb[i];
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
            head = 0;
            wrapped = false;
            xSemaphoreGive(mtx);
        }
    }
}
