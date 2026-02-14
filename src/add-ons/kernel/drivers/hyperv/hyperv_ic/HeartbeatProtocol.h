/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_HEARTBEAT_PROTOCOL_H_
#define _HYPERV_HEARTBEAT_PROTOCOL_H_

#include "ICProtocol.h"

#define HV_HEARTBEAT_RING_SIZE		0x1000

// Heartbeat versions
#define HV_HEARTBEAT_VERSION_V1		MAKE_IC_VERSION(1, 0)
#define HV_HEARTBEAT_VERSION_V3		MAKE_IC_VERSION(3, 0)


typedef struct { // Heartbeat sequence message
	hv_ic_msg_header	header;

	uint64	sequence;
	uint32	reserved;
} _PACKED hv_heartbeat_msg_seq;


typedef struct { // Heartbeat combined message
	union {
		hv_ic_msg_header		header;
		hv_ic_msg_negotiate		negotiate;
		hv_heartbeat_msg_seq	heartbeat;
	};
} _PACKED hv_heartbeat_msg;


#endif // _HYPERV_HEARTBEAT_PROTOCOL_H_
