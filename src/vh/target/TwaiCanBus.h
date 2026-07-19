#pragma once
#include "../../bus/ICanBus.h"
#include "driver/twai.h"

// CR-04: the GW's vehicle-CAN access, behind the one CAN interface (`ICanBus`).
// IDF TWAI driver in LISTEN_ONLY mode by default — no ACK, no error frames,
// physically incapable of disturbing the vehicle bus. send() honestly refuses
// in listen-only (TX mode is a deliberate, explicit opt-in for the future
// OBD-request feature). poll() drains the RX queue into the handler — call it
// every loop().
class TwaiCanBus : public ICanBus {
public:
    bool begin(gpio_num_t txPin, gpio_num_t rxPin, bool listenOnly = true)
    {
        _listenOnly = listenOnly;
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
            txPin, rxPin, listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
        g.rx_queue_len = 32;
        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
        return twai_start() == ESP_OK;
    }

    bool send(const Frame& f) override
    {
        if (_listenOnly) return false;
        twai_message_t m = {};
        m.identifier = f.id;
        m.extd = f.extended;
        m.data_length_code = f.len;
        for (int i = 0; i < f.len && i < 8; i++) m.data[i] = f.data[i];
        return twai_transmit(&m, 0) == ESP_OK;
    }

    void onReceive(RxHandler h, void* ctx) override { _rx = h; _rxCtx = ctx; }

    bool isLive() const override
    {
        return _lastRxMs != 0 && (millis() - _lastRxMs) < 2000;
    }

    void poll() override
    {
        twai_message_t msg;
        while (twai_receive(&msg, 0) == ESP_OK)
        {
            if (msg.rtr) continue;
            _lastRxMs = millis();
            Frame f;
            f.id = msg.identifier;
            f.extended = msg.extd;
            f.source = Frame::SRC_VH_CAN;
            f.len = msg.data_length_code > 8 ? 8 : msg.data_length_code;
            for (int i = 0; i < f.len; i++) f.data[i] = msg.data[i];
            if (_rx) _rx(f, _rxCtx);
        }
    }

    uint32_t lastRxMs() const { return _lastRxMs; }

private:
    bool _listenOnly = true;
    RxHandler _rx = nullptr;
    void* _rxCtx = nullptr;
    volatile uint32_t _lastRxMs = 0;
};
