#pragma once
#include <stdint.h>
#include "VehicleSignals.h"
#include "../bus/Frame.h"

// Data-driven vehicle-CAN decoder (ARCHITECTURE-V2 §5): feeds VehicleState from
// the VehicleDbc.h table. Also keeps the unknown-ID census for the P2 sniff
// campaign. Portable — native tests replay synthetic and .canlog frames into it.
namespace Vh
{
    class VehicleDecoder {
    public:
        // Decode one received frame (nowMs from the caller's clock).
        void feed(const Frame& f, uint32_t nowMs);

        // Expire timestamp latches (indicators) even when no frame arrives.
        void tick(uint32_t nowMs);

        const VehicleState& state() const { return _state; }

        // Per-signal disable (VhConfig override: sniff-phase corrections).
        void setSignalEnabled(uint8_t sig, bool on);
        bool signalEnabled(uint8_t sig) const;

        // Unknown-ID census: ids seen that no table entry matches.
        struct IdCount { uint16_t id; uint32_t count; };
        static constexpr uint8_t CENSUS_SLOTS = 64;
        const IdCount* census(uint8_t& n) const { n = _censusN; return _census; }
        uint32_t knownFrames() const { return _known; }
        uint32_t unknownFrames() const { return _unknown; }

    private:
        void applyField(const struct FieldDef& fd, const Frame& f, uint32_t nowMs);
        void noteUnknown(uint16_t id);

        VehicleState _state;
        uint32_t _sigDisabled = 0;          // bitmask by Sig (fits: SIG_COUNT <= 32)
        IdCount _census[CENSUS_SLOTS] = {};
        uint8_t _censusN = 0;
        uint32_t _known = 0, _unknown = 0;
    };
}
