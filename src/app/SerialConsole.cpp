#include "app/SerialConsole.h"

#include <Arduino.h>
#include <SerialCommands.h>
#include <stdlib.h>

#include "effects/ScrollEffect.h"
#include "app/AppContext.h"
#include "bluetooth/A2dpManager.h"

// These globals already exist in main.cpp
extern AppContext g_app;
extern A2dpManager g_a2dp;

namespace
{
    void cmd_enable(SerialCommands *sender)
    {
        (void)sender;
        Serial.println("Enabling display");
        if (g_app.display)
            g_app.display->setState(true);
    }

    void cmd_disable(SerialCommands *sender)
    {
        (void)sender;
        if (g_app.display)
            g_app.display->setState(false);
    }

    void cmd_playpause(SerialCommands *sender)
    {
        (void)sender;
        g_a2dp.playPause();
    }

    void cmd_next(SerialCommands *sender)
    {
        (void)sender;
        g_a2dp.next();
    }

    void cmd_prev(SerialCommands *sender)
    {
        (void)sender;
        g_a2dp.previous();
    }

    void cmd_scrollmtx(SerialCommands *sender)
    {
        const char *text = sender->Next();
        const char *delayStr = sender->Next();

        if (!text || !g_app.display)
            return;

        uint16_t delayMs = 300;
        if (delayStr)
        {
            delayMs = atoi(delayStr);
            if (delayMs < 20)
                delayMs = 20;
        }

        Serial.println("Scrolling text: ");
        Serial.println(text);
        ScrollEffect(g_app.display, ScrollDirection::Right, text, delayMs);
    }

    void cmd_scrollmtxl(SerialCommands *sender)
    {
        const char *text = sender->Next();
        const char *delayStr = sender->Next();

        if (!text || !g_app.display)
            return;

        uint16_t delayMs = 300;
        if (delayStr)
        {
            delayMs = atoi(delayStr);
            if (delayMs < 20)
                delayMs = 20;
        }

        ScrollEffect(g_app.display, ScrollDirection::Left, text, delayMs);
    }

    void cmd_setTime(SerialCommands *sender)
    {
        char *timeStr = sender->Next();
        if (!timeStr || !g_app.display)
        {
            Serial.println("Usage: st <HHMM>");
            return;
        }

        g_app.display->setTime(timeStr);
    }

    void cmd_btinfo(SerialCommands *sender)
    {
        (void)sender;
        Serial.printf("[A2DP] connected=%s playing=%s\n",
                      g_a2dp.isConnected() ? "yes" : "no",
                      g_a2dp.isPlaying() ? "yes" : "no");

        const auto &track = g_a2dp.trackInfo();
        Serial.printf("[A2DP] title=%s\n", track.title.c_str());
        Serial.printf("[A2DP] artist=%s\n", track.artist.c_str());
        Serial.printf("[A2DP] album=%s\n", track.album.c_str());
    }

    SerialCommand cmd_e("e", cmd_enable);
    SerialCommand cmd_d("d", cmd_disable);
    SerialCommand cmd_st("st", cmd_setTime);
    SerialCommand cmd_msr("msr", cmd_scrollmtx);
    SerialCommand cmd_msl("msl", cmd_scrollmtxl);
    SerialCommand cmd_pp("pp", cmd_playpause);
    SerialCommand cmd_nx("nx", cmd_next);
    SerialCommand cmd_pv("pv", cmd_prev);
    SerialCommand cmd_bti("bti", cmd_btinfo);

    char serial_command_buffer[64];
    char serial_delim[] = " \r\n";
    SerialCommands serialCommands(&Serial, serial_command_buffer, sizeof(serial_command_buffer), serial_delim);

    bool commandsRegistered = false;
}

void SerialConsole::begin()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");

    if (!commandsRegistered)
    {
        serialCommands.AddCommand(&cmd_e);
        serialCommands.AddCommand(&cmd_d);
        serialCommands.AddCommand(&cmd_st);
        serialCommands.AddCommand(&cmd_msr);
        serialCommands.AddCommand(&cmd_msl);
        serialCommands.AddCommand(&cmd_pp);
        serialCommands.AddCommand(&cmd_nx);
        serialCommands.AddCommand(&cmd_pv);
        serialCommands.AddCommand(&cmd_bti);
        commandsRegistered = true;
    }
}

void SerialConsole::tick()
{
    serialCommands.ReadSerial();
}