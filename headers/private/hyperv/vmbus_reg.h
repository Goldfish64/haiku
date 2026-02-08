/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef VMBUS_REG_H
#define VMBUS_REG_H

// HID of VMBus ACPI device
// This is normally just "VMBus", but acpica seems to need all caps
#define VMBUS_ACPI_HID_NAME		"VMBUS"

// Fixed interrupt for VMBus messages
#define VMBUS_SINT_MESSAGE		2
// Fixed interrupt for VMBus timers
#define VMBUS_SINT_TIMER		4

// Fixed connection ID for messages
#define VMBUS_CONNID_MESSAGE		1
// Fixed connection ID for events
#define VMBUS_CONNID_EVENTS			2

#define MAKE_VMBUS_VERSION(major, minor)    ((((major) << 16) & 0xFFFF0000) | ((minor) & 0x0000FFFF))
#define GET_VMBUS_VERSION_MAJOR(version)    (((version) >> 16) & 0xFFFF)
#define GET_VMBUS_VERSION_MINOR(version)    ((version) & 0xFFFF)

#define VMBUS_VERSION_WS2008              MAKE_VMBUS_VERSION(0, 13)
#define VMBUS_VERSION_WS2008R2            MAKE_VMBUS_VERSION(1, 1)
#define VMBUS_VERSION_WIN8_WS2012         MAKE_VMBUS_VERSION(2, 4)
#define VMBUS_VERSION_WIN81_WS2012R2      MAKE_VMBUS_VERSION(3, 0)
#define VMBUS_VERSION_WIN10_RS1_WS2016    MAKE_VMBUS_VERSION(4, 0)
#define VMBUS_VERSION_WIN10_RS3           MAKE_VMBUS_VERSION(4, 1)
#define VMBUS_VERSION_WIN10_V5            MAKE_VMBUS_VERSION(5, 0)
#define VMBUS_VERSION_WIN10_RS4           MAKE_VMBUS_VERSION(5, 1)
#define VMBUS_VERSION_WIN10_RS5_WS2019    MAKE_VMBUS_VERSION(5, 2)
#define VMBUS_VERSION_WS2022              MAKE_VMBUS_VERSION(5, 3)

enum { // VMBus message type
	VMBUS_MSGTYPE_INVALID					= 0,
	VMBUS_MSGTYPE_CHANNEL_OFFER				= 1,
	VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER		= 2,
	VMBUS_MSGTYPE_REQUEST_CHANNELS			= 3,
	VMBUS_MSGTYPE_REQUEST_CHANNELS_DONE		= 4,
	VMBUS_MSGTYPE_OPEN_CHANNEL				= 5,
	VMBUS_MSGTYPE_OPEN_CHANNEL_RESPONSE		= 6,
	VMBUS_MSGTYPE_CLOSE_CHANNEL				= 7,
	VMBUS_MSGTYPE_CREATE_GPADL				= 8,
	VMBUS_MSGTYPE_CREATE_GPADL_PAGES		= 9,
	VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE		= 10,
	VMBUS_MSGTYPE_REMOVE_GPADL				= 11,
	VMBUS_MSGTYPE_REMOVE_GPADL_RESPONSE		= 12,
	VMBUS_MSGTYPE_FREE_CHANNEL				= 13,
	VMBUS_MSGTYPE_CONNECT					= 14,
	VMBUS_MSGTYPE_CONNECT_RESPONSE			= 15,
	VMBUS_MSGTYPE_DISCONNECT				= 16,
	VMBUS_MSGTYPE_DISCONNECT_RESPONSE		= 17,
	VMBUS_MSGTYPE_MODIFY_CHANNEL			= 22,
	VMBUS_MSGTYPE_MODIFY_CHANNEL_RESPONSE	= 24,
	VMBUS_MSGTYPE_MAX
};

typedef struct { // VMBus message header
	uint32	type;
	uint32	reserved;
} _PACKED vmbus_msg_header;

typedef struct { // VMBus channel offer message from Hyper-V
	vmbus_msg_header header;

	uuid_t	type_id;
	uuid_t	instance_id;
	uint64	reserved1[2];

	uint16	flags;
	uint16	mmio_size_mb;

	union {
		struct {
#define VMBUS_CHANNEL_OFFER_MAX_USER_BYTES      120
			uint8	data[VMBUS_CHANNEL_OFFER_MAX_USER_BYTES];
		} standard;
		struct {
			uint32	mode;
			uint8	data[VMBUS_CHANNEL_OFFER_MAX_USER_BYTES - 4];
		} pipe;
	};

	uint16	sub_index;
	uint16	reserved2;
	uint32	channel_id;
	uint8	monitor_id;

	// Fields present only in Server 2008 R2 and newer
	uint8	monitor_alloc	: 1;
	uint8	reserved3		: 7;
	uint16	dedicated_int	: 1;
	uint16	reserved4		: 15;

	uint32	conn_id;
} _PACKED vmbus_msg_channel_offer;

typedef struct { // VMBus rescind channel offer message from Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
} _PACKED vmbus_msg_rescind_channel_offer;

typedef struct { // VMBus request channels message to Hyper-V
	vmbus_msg_header header;
} _PACKED vmbus_msg_request_channels;

typedef struct { // VMBus request channels done message from Hyper-V
	vmbus_msg_header header;
} _PACKED vmbus_msg_request_channels_done;

typedef struct { // VMBus open channel message to Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
	uint32	open_id;
	uint32	gpadl_id;
	uint32	target_cpu;
	uint32	rx_page_offset;
	uint8	user_data[VMBUS_CHANNEL_OFFER_MAX_USER_BYTES];
}  _PACKED vmbus_msg_open_channel;

typedef struct { // VMBus open channel response message from Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
	uint32	open_id;
	uint32	result;
}  _PACKED vmbus_msg_open_channel_resp;

typedef struct { // VMBus connect message to Hyper-V
	vmbus_msg_header header;

	uint32	version;
	uint32	target_cpu;

	uint64	event_flags_physaddr;
	uint64	monitor1_physaddr;
	uint64 	monitor2_physaddr;
} _PACKED vmbus_msg_connect;

typedef struct { // VMBus connect response message from Hyper-V
	vmbus_msg_header header;

	uint8	supported;
	uint8	connection_state;
	uint16	reserved;
	uint32	connection_id;
} _PACKED vmbus_msg_connect_resp;

typedef union { // VMBus combined message
	vmbus_msg_header	header;

	vmbus_msg_channel_offer				channel_offer;
	vmbus_msg_rescind_channel_offer		rescind_channel_offer;
	vmbus_msg_request_channels			request_channels;
	vmbus_msg_request_channels_done		request_channels_done;
	vmbus_msg_open_channel				open_channel;
	vmbus_msg_open_channel_resp			open_channel_resp;
	vmbus_msg_connect					connect;
	vmbus_msg_connect_resp				connect_resp;
} _PACKED vmbus_msg;

#endif
