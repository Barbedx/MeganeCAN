#ifndef __CAN_H
#define __CAN_H

#include <stdint.h>

#define CAN_MAX_LEN      8

struct can_packet {
	uint16_t id;
	uint8_t len;
	uint8_t data[CAN_MAX_LEN];
};

#endif /* __CAN_H */
