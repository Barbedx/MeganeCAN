#pragma once
#include <Arduino.h>

// /affa3test dev page (extracted from HttpServerManager.cpp).
static const char AFFA3TEST_PAGE[] PROGMEM = R"rawliteral(
        <!DOCTYPE html>
        <html><head><title>Affa3 Display Test</title></head><body>
        <h2>Set Menu</h2>
        <form action="/affa3/setMenu" method="GET">
            Caption: <input name="caption" required><br>
            Name1: <input name="name1" required><br>
            Name2: <input name="name2" required><br>
            Scroll Lock (Hex): <input name="scrollLock" value="0B" pattern="[0-9a-fA-F]{2}" ><br>
            <input type="submit" value="Set Menu">
        </form>
        <h2>Set AUX</h2>
        <form action="/affa3/setAux" method="GET">
            <input type="submit" value="Set AUX">
        </form>
        <h2>Set Big Text</h2>
        <form action="/affa3/setTextBig" method="GET">
            Caption: <input name="caption" required><br>
            Row1: <input name="row1" required><br>
            Row2: <input name="row2" required><br>
            <input type="submit" value="Set Big Text">
        </form>
        </body></html>
)rawliteral";
