#include "UartCanboxSink.h"
#include "../link/mm/MmLinkService.h"

void UartCanboxSink::onKey(AffaCommon::AffaKey key, bool isHold)
{
    // Lost frames must never stick a key (§10): the release is an edge event
    // sent right behind the press; VH forwards both with priority.
    uint16_t code = AffaCommon::to_uint16(key);
    MmLink::sendKey(code, isHold ? 2 : 1);   // 2 = long, 1 = press
    MmLink::sendKey(code, 0);                // release
}
