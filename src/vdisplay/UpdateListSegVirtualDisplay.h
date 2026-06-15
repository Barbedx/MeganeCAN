#pragma once
#include "VirtualDisplayBase.h"
#include "../display/UpdateList/UpdateListConstants.h"

// Shared UpdateList protocol descriptor (both the 8-seg and the mono-LCD panels use
// these IDs; they differ only in the screen payload they decode).
inline VdProtocol updateListProtocol() {
    return VdProtocol{
        /*ctrlId*/ UpdateList::PACKET_ID_DISPLAY_CTRL,   // 0x1B1
        /*textId*/ UpdateList::PACKET_ID_SETTEXT,        // 0x121
        /*keyId */ UpdateList::PACKET_ID_KEYPRESSED,     // 0x0A9
        /*syncId*/ UpdateList::PACKET_ID_SYNC,           // 0x3DF
        /*syncReplyId*/ UpdateList::PACKET_ID_SYNC_REPLY,// 0x3CF
        /*replyFlag*/ UpdateList::PACKET_REPLY_FLAG,     // 0x400
        /*filler*/ UpdateList::PACKET_FILLER};           // 0x81
}

// Virtual 8-segment UpdateList panel (text-only). Decodes the setText payload on
// 0x121 (10 19 76 …) into ScreenModel.header (new text) / item0 (old text).
class UpdateListSegVirtualDisplay : public VirtualDisplayBase {
public:
    UpdateListSegVirtualDisplay() : VirtualDisplayBase(updateListProtocol()) {}

protected:
    void decode(const Frame& f) override;
};
