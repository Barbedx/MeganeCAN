#pragma once

// #include <PsychicHttp.h>
#include "display/IDisplay.h"
// #include <Preferences.h>
#include "ELMduino.h"
#include "commands/DisplayCommands.h" // Include the DisplayCommands manager
#include "WiFi.h"
struct QueryData
{
    const char *modeAndPid;
    const char *name;
    float (*evalFunc)(const uint8_t *payload);
};

struct QueryGroup
{
    const char *header;
    const QueryData *queries;
    size_t queryCount;
};
class MyELMManager
{
public:
    MyELMManager(IDisplay &display);
    enum QueryState
    {
        SEND_COMMAND,
        WAITING_RESP
    };
    void setup()
    {
        elm.begin(wifiClient, true, 2000);
    }

    void tick()
    {
        static QueryState nb_query_state = SEND_COMMAND;
        static size_t currentGroup = 0;
        static size_t currentQuery = 0;
        static unsigned long lastQueryTime = 0;

        // Throttle queries so we don't spam the bus
        const unsigned long queryInterval = 500; // ms
        if (millis() - lastQueryTime < queryInterval)
            return;

        const QueryGroup &group = queryList[currentGroup];
        const QueryData &query = group.queries[currentQuery];

        Serial.printf("[Tick] Group %zu Query %zu State %s\n",
                      currentGroup, currentQuery,
                      nb_query_state == SEND_COMMAND ? "SEND_COMMAND" : "WAITING_RESP");

        switch (nb_query_state)
        {
        case SEND_COMMAND:
            // Set header at the start of a group
            nb_query_state = WAITING_RESP;
            if (currentQuery == 0)
            {
                Serial.printf("[Header] Sending header: %s\n", group.header);

                elm.sendCommand(group.header);
                return;
            }
            // Send PID query
            else
            {

                // Send PID query
                Serial.printf("[Query] Sending PID: %s (%s)\n", query.modeAndPid, query.name);

                elm.sendCommand(query.modeAndPid);
            }
            Serial.println("Query sended!");
            lastQueryTime = millis();
            break;

        case WAITING_RESP:
            elm.get_response(); // Check if a response is ready
            if (elm.nb_rx_state == ELM_SUCCESS)
            {

                // Log raw payload
                Serial.print("[Payload] ");
                for (size_t i = 0; i < elm.PAYLOAD_LEN; i++)
                {
                    Serial.printf("%02X ", elm.payload[i]);
                }
                Serial.println();

                float value = query.evalFunc(reinterpret_cast<const uint8_t *>(elm.payload));
                Serial.printf("[Response] [%s][%s] = %f\n", group.header, query.name, value);

                // Example: push to display
                // if (strcmp(query.name, "PR_ЗАРЯД ОТ ГЕНЕРАТОРА") == 0)
                // {
                //     Manager::setVoltage((int)value); TODO://move to callbacks
                // }

                Serial.printf("[%s][%s] = %f\n", group.header, query.name, value); // TODO:add payload

                nb_query_state = SEND_COMMAND;
                nextQuery(currentGroup, currentQuery);
                lastQueryTime = millis();
            }
            else if (elm.nb_rx_state != ELM_GETTING_MSG)
            {
                // Error: reset query state
                Serial.printf("Query %s failed or timed out\n", query.modeAndPid);
                elm.printError();
                nb_query_state = SEND_COMMAND;
                nextQuery(currentGroup, currentQuery);
                lastQueryTime = millis();
            }
            break;
        }
    };

    static uint8_t getByte(const uint8_t *p, char symbol)
    {
        switch (symbol)
        {
        case 'A':
            return p[0];
        case 'B':
            return p[1];
        case 'C':
            return p[2];
        case 'D':
            return p[3];
        case 'E':
            return p[4];
        case 'F':
            return p[5];
        case 'G':
            return p[6];
        case 'H':
            return p[7];
        case 'I':
            return p[8];
        case 'J':
            return p[9];
        case 'K':
            return p[10];
        case 'L':
            return p[11];
        case 'M':
            return p[12];
        case 'N':
            return p[13];
        case 'O':
            return p[14];
        case 'P':
            return p[15];
        case 'U':
            return p[20];
        case 'V':
            return p[21];
        case 'W':
            return p[22];
        case 'Z':
            return p[25];
        }
        return 0;
    }

private:
    // ========================
    // Byte helper
    // ========================
    IDisplay &display;
    ELM327 elm;
    WiFiClient wifiClient;

    using EvalFunc = std::function<float(int, int)>;

    // декларації
    static const QueryData queries_74D[2];
    static const QueryData queries_743[2];
    static const QueryData queries_744[2];
    static const QueryData queries_745[3];
    static const std::array<QueryGroup, 4> queryList;

    // move to next query/group
    static void nextQuery(size_t &group, size_t &query)
    {
        query++;
        if (query >= queryList[group].queryCount)
        {
            query = 0;
            group++;
            if (group >= (sizeof(queryList) / sizeof(QueryGroup)))
            {
                group = 0;
            }
        }
    }
    // ========================
    // Queries grouped by header (формули inline)
    // ========================
};