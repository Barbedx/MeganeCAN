#pragma once
#include "VirtualDisplayBase.h"
#include "../display/Carminat/CarminatConstants.h"

// Virtual monochrome Carminat panel. All Carminat screens (menu / now-playing /
// notification) are showMenu over 0x151, so one decode path covers them; selection
// arrives as a separate 07 29 01 highlight frame.
class CarminatVirtualDisplay : public VirtualDisplayBase {
public:
    CarminatVirtualDisplay()
        : VirtualDisplayBase(VdProtocol{
              /*ctrlId*/ Carminat::PACKET_ID_SETTEXT,        // 0x151
              /*textId*/ Carminat::PACKET_ID_SETTEXT,        // 0x151 (same channel)
              /*keyId */ Carminat::PACKET_ID_KEYPRESSED,     // 0x1C1
              /*syncId*/ Carminat::PACKET_ID_SYNC,           // 0x3AF
              /*syncReplyId*/ Carminat::PACKET_ID_SYNC_REPLY,// 0x3CF
              /*replyFlag*/ Carminat::PACKET_REPLY_FLAG,     // 0x400
              /*filler*/ Carminat::PACKET_FILLER}) {}        // 0x00

protected:
    void decode(const Frame& f) override;
};
