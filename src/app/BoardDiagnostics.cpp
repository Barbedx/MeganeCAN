#include "app/BoardDiagnostics.h"

#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>

namespace
{
    const char* yesNo(bool v)
    {
        return v ? "YES" : "NO";
    }
}

namespace BoardDiagnostics
{
    void printReport()
    {
        esp_chip_info_t chipInfo{};
        esp_chip_info(&chipInfo);

        Serial.println(F("------------------------------------------------------------"));
        Serial.println(F("BOARD REPORT"));
        Serial.println(F("------------------------------------------------------------"));

    #if defined(CONFIG_IDF_TARGET_ESP32)
        Serial.println(F("Target              : ESP32"));
    #elif defined(CONFIG_IDF_TARGET_ESP32S3)
        Serial.println(F("Target              : ESP32-S3"));
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
        Serial.println(F("Target              : ESP32-C3"));
    #elif defined(CONFIG_IDF_TARGET_ESP32C6)
        Serial.println(F("Target              : ESP32-C6"));
    #elif defined(CONFIG_IDF_TARGET_ESP32H2)
        Serial.println(F("Target              : ESP32-H2"));
    #else
        Serial.println(F("Target              : UNKNOWN"));
    #endif

        Serial.printf("Cores               : %d\n", chipInfo.cores);
        Serial.printf("Revision            : %d\n", chipInfo.revision);
        Serial.printf("Classic BT support   : %s\n", yesNo(SOC_CLASSIC_BT_SUPPORTED));

    #if defined(SOC_BLE_SUPPORTED)
        Serial.printf("BLE support          : %s\n", yesNo(SOC_BLE_SUPPORTED));
    #endif

        Serial.printf("Flash size           : %u bytes (%.2f MB)\n",
                      ESP.getFlashChipSize(),
                      ESP.getFlashChipSize() / 1024.0 / 1024.0);

        Serial.printf("Sketch size          : %u bytes (%.2f KB)\n",
                      ESP.getSketchSize(),
                      ESP.getSketchSize() / 1024.0);

        Serial.printf("Free sketch space    : %u bytes (%.2f KB)\n",
                      ESP.getFreeSketchSpace(),
                      ESP.getFreeSketchSpace() / 1024.0);

        Serial.printf("Heap total           : %u bytes (%.2f KB)\n",
                      ESP.getHeapSize(),
                      ESP.getHeapSize() / 1024.0);

        Serial.printf("Heap free            : %u bytes (%.2f KB)\n",
                      ESP.getFreeHeap(),
                      ESP.getFreeHeap() / 1024.0);

        Serial.printf("Heap min free        : %u bytes (%.2f KB)\n",
                      ESP.getMinFreeHeap(),
                      ESP.getMinFreeHeap() / 1024.0);

        Serial.printf("Largest alloc heap   : %u bytes (%.2f KB)\n",
                      ESP.getMaxAllocHeap(),
                      ESP.getMaxAllocHeap() / 1024.0);

        Serial.printf("PSRAM total          : %u bytes (%.2f KB)\n",
                      ESP.getPsramSize(),
                      ESP.getPsramSize() / 1024.0);

        Serial.printf("PSRAM free           : %u bytes (%.2f KB)\n",
                      ESP.getFreePsram(),
                      ESP.getFreePsram() / 1024.0);

        const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);

        Serial.printf("Internal RAM free    : %u bytes (%.2f KB)\n",
                      static_cast<unsigned>(internalFree),
                      internalFree / 1024.0);

        Serial.printf("DMA-capable free     : %u bytes (%.2f KB)\n",
                      static_cast<unsigned>(dmaFree),
                      dmaFree / 1024.0);
    }
}