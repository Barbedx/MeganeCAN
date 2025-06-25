#include <stdint.h>

enum class Affa3Error : uint8_t {
    NoError     = 0,
    NoSync       = 0x01,
    UnknownFunc  = 0x02,
    SendFailed   = 0x03,
    Timeout      = 0x04,
    StrTooLong   = 0x05 
};