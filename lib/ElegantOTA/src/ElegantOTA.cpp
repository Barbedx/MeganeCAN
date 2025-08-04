#include "ElegantOTA.h"

ElegantOTAClass::ElegantOTAClass() {}


// Declare upload handler as a member or global pointer
PsychicUploadHandler* otaUploadHandler = nullptr;

void ElegantOTAClass::begin(ELEGANTOTA_WEBSERVER *server, const char *username, const char *password)
{
  _server = server;

  setAuth(username, password);
  otaUploadHandler = new PsychicUploadHandler();
otaUploadHandler->onUpload([](PsychicRequest* request, const String& filename, uint64_t index, uint8_t* data, size_t len, bool last) -> esp_err_t {
    static size_t currentProgressSize = 0;

    if (index == 0) {
        Serial.printf("OTA Update Start: %s\n", filename.c_str());
        currentProgressSize = 0;

        // Here you can check authentication if needed (pseudo code)
        // if (authentication_fails) return ESP_FAIL;

        if (!Update.begin()) {  // Start the update
            Update.printError(Serial);
            return ESP_FAIL;
        }
    }

    // Write chunk to update
    if (Update.write(data, len) != (int)len) {
        Update.printError(Serial);
        return ESP_FAIL;
    }

    currentProgressSize += len;

    // Optionally, call progress callback if you have one
    // e.g., progressUpdateCallback(currentProgressSize, request->contentLength());

    if (last) {
        if (Update.end(true)) { // Finalize update and reboot if successful
            Serial.printf("OTA Update Success, total bytes: %llu\n", index + len);
        } else {
            Serial.println("OTA Update Failed");
            Update.printError(Serial);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
});

otaUploadHandler->onRequest([](PsychicRequest* request) {
    // Send back response after upload finished
    String responseText;
    if (Update.hasError()) {
        StreamString errStr;
        Update.printError(errStr);
        responseText = "Update failed:\n" + String(errStr.c_str());
        return request->reply(400, "text/plain", responseText.c_str());
    } else {
        responseText = "Update OK";
        // Optionally schedule reboot here if needed
        // e.g. _reboot_request_millis = millis(); _reboot = true;
        return request->reply(200, "text/plain", responseText.c_str());
    }
});

// Register the upload handler for the OTA upload endpoint (wildcard for filename)
_server->on("/ota/upload/*", HTTP_POST, otaUploadHandler);

#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
  if (!__isPicoW)
  {
    ELEGANTOTA_DEBUG_MSG("RP2040: Not a Pico W, skipping OTA setup\n");
    return;
  }
#endif


  _server->on("/update", HTTP_GET, [this](PsychicRequest *request)
              {
                // if (_authenticate && !_server-> authenticate(_username.c_str(), _password.c_str())) {
                //   return _server->requestAuthentication();
                // }
                PsychicResponse response(request);

                // Add headers
                response.addHeader("Content-Encoding", "gzip");
                response.addHeader("Cache-Control", "no-cache");

                // Set HTTP response code
                response.setCode(200);
                // Set content type
                response.setContentType("text/html");

                // IMPORTANT: Set the binary content pointer and size.
                // Since ELEGANT_HTML is uint8_t[], cast to const char* safely:
                response.setContent(ELEGANT_HTML, sizeof(ELEGANT_HTML));

                return response.send();
              });
 
\
  _server->on("/ota/start", HTTP_GET, [this](PsychicRequest *request)
              {
      // if (_authenticate && !_server->authenticate(_username.c_str(), _password.c_str())) {
      //   return _server->requestAuthentication();
      // }

      // Get header x-ota-mode value, if present
      OTA_Mode mode = OTA_MODE_FIRMWARE;
      // Get mode from arg
      if (request->hasParam("mode")) {
        String argValue = request->getParam("mode")->value();
        if (argValue == "fs") {
          ELEGANTOTA_DEBUG_MSG("OTA Mode: Filesystem\n");
          mode = OTA_MODE_FILESYSTEM;
        } else {
          ELEGANTOTA_DEBUG_MSG("OTA Mode: Firmware\n");
          mode = OTA_MODE_FIRMWARE;
        }
      }

      // Get file MD5 hash from arg
      if (request->hasParam("hash")) {
        String hash = request->getParam("hash")->value();
        ELEGANTOTA_DEBUG_MSG(String("MD5: "+hash+"\n").c_str());
        if (!Update.setMD5(hash.c_str())) {
          ELEGANTOTA_DEBUG_MSG("ERROR: MD5 hash not valid\n");
          return request->reply(400, "text/plain", "MD5 parameter invalid");
        }
      }

#if UPDATE_DEBUG == 1
        // Serial output must be active to see the callback serial prints
        Serial.setDebugOutput(true);
#endif

      // Pre-OTA update callback
      if (preUpdateCallback != NULL) preUpdateCallback();

// Start update process
#if defined(ESP8266)
        uint32_t update_size = mode == OTA_MODE_FILESYSTEM ? ((size_t)FS_end - (size_t)FS_start) : ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
        if (mode == OTA_MODE_FILESYSTEM) {
          close_all_fs();
        }
        Update.runAsync(true);
        if (!Update.begin(update_size, mode == OTA_MODE_FILESYSTEM ? U_FS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
#elif defined(ESP32)  
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
        uint32_t update_size = 0;
        // Gather FS Size
        if (mode == OTA_MODE_FILESYSTEM) {
          update_size = ((size_t)&_FS_end - (size_t)&_FS_start);
          LittleFS.end();
        } else {
          FSInfo i;
          LittleFS.begin();
          LittleFS.info(i);
          update_size = i.totalBytes - i.usedBytes;
        }
        // Start update process
        if (!Update.begin(update_size, mode == OTA_MODE_FILESYSTEM ? U_FS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
#endif

      return request->reply((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? _update_error_str.c_str() : "OK"); });

}

void ElegantOTAClass::setAuth(const char *username, const char *password)
{
  _username = username;
  _password = password;
  _authenticate = _username.length() && _password.length();
}

void ElegantOTAClass::clearAuth()
{
  _authenticate = false;
}

void ElegantOTAClass::setAutoReboot(bool enable)
{
  _auto_reboot = enable;
}

void ElegantOTAClass::loop()
{
  // Check if 2 seconds have passed since _reboot_request_millis was set
  if (_reboot && millis() - _reboot_request_millis > 2000)
  {
    ELEGANTOTA_DEBUG_MSG("Rebooting...\n");
#if defined(ESP8266) || defined(ESP32)
    ESP.restart();
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
    rp2040.reboot();
#endif
    _reboot = false;
  }
}

void ElegantOTAClass::onStart(std::function<void()> callable)
{
  preUpdateCallback = callable;
}

void ElegantOTAClass::onProgress(std::function<void(size_t current, size_t final)> callable)
{
  progressUpdateCallback = callable;
}

void ElegantOTAClass::onEnd(std::function<void(bool success)> callable)
{
  postUpdateCallback = callable;
}

ElegantOTAClass ElegantOTA;
