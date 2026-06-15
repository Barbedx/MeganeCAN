#pragma once
#include "UpdateListSegVirtualDisplay.h"   // reuses updateListProtocol()

// Virtual monochrome-LCD UpdateList panel. Same IDs as the 8-seg panel; the mono LCD
// renders richer screens. PROVISIONAL: this decodes the text channel (0x121) as a
// showMenu payload (the hypothesis that the mono UL panel reuses the Carminat-style
// 96-byte layout). This path is the least-exercised of the three and must be
// validated against a real mono UpdateList display before it is trusted — see the
// plan's risk note. The protocol mechanics it inherits (auto-ACK, sync, key TX) are
// shared + tested; only the screen-payload offsets are unconfirmed.
class UpdateListLcdVirtualDisplay : public VirtualDisplayBase {
public:
    UpdateListLcdVirtualDisplay() : VirtualDisplayBase(updateListProtocol()) {}

protected:
    void decode(const Frame& f) override;
};
