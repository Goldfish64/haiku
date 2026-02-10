/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _VMBUS_REG_H_
#define _VMBUS_REG_H_

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

#define MAKE_VMBUS_VERSION(major, minor)	((((major) << 16) & 0xFFFF0000) | ((minor) & 0x0000FFFF))
#define GET_VMBUS_VERSION_MAJOR(version)	(((version) >> 16) & 0xFFFF)
#define GET_VMBUS_VERSION_MINOR(version)	((version) & 0xFFFF)

#define VMBUS_VERSION_WS2008				MAKE_VMBUS_VERSION(0, 13)
#define VMBUS_VERSION_WS2008R2				MAKE_VMBUS_VERSION(1, 1)
#define VMBUS_VERSION_WIN8_WS2012			MAKE_VMBUS_VERSION(2, 4)
#define VMBUS_VERSION_WIN81_WS2012R2		MAKE_VMBUS_VERSION(3, 0)
#define VMBUS_VERSION_WIN10_RS1_WS2016		MAKE_VMBUS_VERSION(4, 0)
#define VMBUS_VERSION_WIN10_RS3				MAKE_VMBUS_VERSION(4, 1)
#define VMBUS_VERSION_WIN10_V5				MAKE_VMBUS_VERSION(5, 0)
#define VMBUS_VERSION_WIN10_RS4				MAKE_VMBUS_VERSION(5, 1)
#define VMBUS_VERSION_WIN10_RS5_WS2019		MAKE_VMBUS_VERSION(5, 2)
#define VMBUS_VERSION_WS2022				MAKE_VMBUS_VERSION(5, 3)

// VMBus device types
#define VMBUS_TYPE_AVMA				"3375baf4-9e15-4b30-b765-67acb10d607b"
#define VMBUS_TYPE_BALLOON			"525074dc-8985-46e2-8057-a307dc18a502"
#define VMBUS_TYPE_DISPLAY			"da0a7802-e377-4aac-8e77-0558eb1073f8"
#define VMBUS_TYPE_FIBRECHANNEL		"2f9bcc4a-0069-4af3-b76b-6fd0be528cda"
#define VMBUS_TYPE_FILECOPY			"34d14be3-dee4-41c8-9ae7-6b174977c192"
#define VMBUS_TYPE_HEARTBEAT		"57164f39-9115-4e78-ab55-382f3bd5422d"
#define VMBUS_TYPE_IDE				"32412632-86cb-44a2-9b5c-50d1417354f5"
#define VMBUS_TYPE_INPUT			"cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a"
#define VMBUS_TYPE_KEYBOARD			"f912ad6d-2b17-48ea-bd65-f927a61c7684"
#define VMBUS_TYPE_KVP				"a9a0f4e7-5a45-4d96-b827-8a841e8c03e6"
#define VMBUS_TYPE_NETWORK			"f8615163-df3e-46c5-913f-f2d2f965ed0e"
#define VMBUS_TYPE_PCI				"44c4f61d-4444-4400-9d52-802e27ede19f"
#define VMBUS_TYPE_RDCONTROL		"f8e65716-3cb3-4a06-9a60-1889c5cccab5"
#define VMBUS_TYPE_RDMA				"8c2eaf3d-32a7-4b09-ab99-bd1f1c86b501"
#define VMBUS_TYPE_RDVIRT			"276aacf4-ac15-426c-98dd-7521ad3f01fe"
#define VMBUS_TYPE_SCSI				"ba6163d9-04a1-4d29-b605-72e2ffb1dc7f"
#define VMBUS_TYPE_SHUTDOWN			"0e0b6031-5213-4934-818b-38d90ced39db"
#define VMBUS_TYPE_TIMESYNC			"9527e630-d0ae-497b-adce-e80ab0175caf"
#define VMBUS_TYPE_VSS				"35fa2e29-ea23-4236-96ae-3a6ebacba440"

typedef struct {
	uint32	data1;
	uint16	data2;
	uint16	data3;
	uint8	data4[8];
} _PACKED vmbus_guid_t;

typedef struct { // VMBus GPADL range descriptor
	uint32	length;
	uint32	offset;
	uint64	page_nums[];
} _PACKED vmbus_gpadl_range;

#define VMBUS_GPADL_NULL		0
#define VMBUS_GPADL_MAX_PAGES	8192

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
	VMBUS_MSGTYPE_CREATE_GPADL_ADDT			= 9,
	VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE		= 10,
	VMBUS_MSGTYPE_FREE_GPADL				= 11,
	VMBUS_MSGTYPE_FREE_GPADL_RESPONSE		= 12,
	VMBUS_MSGTYPE_FREE_CHANNEL				= 13,
	VMBUS_MSGTYPE_CONNECT					= 14,
	VMBUS_MSGTYPE_CONNECT_RESPONSE			= 15,
	VMBUS_MSGTYPE_DISCONNECT				= 16,
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

	vmbus_guid_t	type_id;
	vmbus_guid_t	instance_id;
	uint64			reserved1[2];
	uint16			flags;
	uint16			mmio_size_mb;

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
} _PACKED vmbus_msg_open_channel;

typedef struct { // VMBus open channel response message from Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
	uint32	open_id;
	uint32	result;
} _PACKED vmbus_msg_open_channel_resp;

typedef struct { // VMBus close channel message to Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
} _PACKED vmbus_msg_close_channel;

typedef struct { // VMBus create GPADL message to Hyper-V
	vmbus_msg_header header;

	uint32				channel_id;
	uint32				gpadl_id;
	uint16				total_range_length;
	uint16				range_count;
	vmbus_gpadl_range	ranges[1]; // 1 range only is supported by this driver
} _PACKED vmbus_msg_create_gpadl;
#define VMBUS_MSG_CREATE_GPADL_MAX_PAGES \
	((HYPERCALL_MAX_DATA_SIZE - sizeof(vmbus_msg_create_gpadl)) / sizeof(uint64))

typedef struct { // VMBus create GPADL additional pages message to Hyper-V
	vmbus_msg_header header;

	uint32	msg_num;
	uint32	gpadl_id;
	uint64	page_nums[];
} _PACKED vmbus_msg_create_gpadl_addt;
#define VMBUS_MSG_CREATE_GPADL_ADDT_MAX_PAGES \
	((HYPERCALL_MAX_DATA_SIZE - sizeof(vmbus_msg_create_gpadl_addt)) / sizeof(uint64))

typedef struct { // VMBus create GPADL response message from Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
	uint32	gpadl_id;
	uint32	result;
} _PACKED vmbus_msg_create_gpadl_resp;

typedef struct { // VMBus free GPADL message to Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
	uint32	gpadl_id;
} _PACKED vmbus_msg_free_gpadl;

typedef struct { // VMBus free GPADL message from Hyper-V
	vmbus_msg_header header;

	uint32	gpadl_id;
} _PACKED vmbus_msg_free_gpadl_resp;

typedef struct { // VMBus free channel message to Hyper-V
	vmbus_msg_header header;

	uint32	channel_id;
} _PACKED vmbus_msg_free_channel;

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

typedef struct { // VMBus disconnect message to Hyper-V
	vmbus_msg_header header;
} _PACKED vmbus_msg_disconnect;

typedef union { // VMBus combined message
	vmbus_msg_header	header;

	vmbus_msg_channel_offer				channel_offer;
	vmbus_msg_rescind_channel_offer		rescind_channel_offer;
	vmbus_msg_request_channels			request_channels;
	vmbus_msg_request_channels_done		request_channels_done;
	vmbus_msg_open_channel				open_channel;
	vmbus_msg_open_channel_resp			open_channel_resp;
	vmbus_msg_close_channel				close_channel;
	vmbus_msg_create_gpadl				create_gpadl;
	vmbus_msg_create_gpadl_addt			create_gpadl_addt;
	vmbus_msg_create_gpadl_resp			create_gpadl_resp;
	vmbus_msg_free_gpadl				free_gpadl;
	vmbus_msg_free_gpadl_resp			free_gpadl_resp;
	vmbus_msg_free_channel				free_channel;
	vmbus_msg_connect					connect;
	vmbus_msg_connect_resp				connect_resp;
	vmbus_msg_disconnect				disconnect;
} _PACKED vmbus_msg;

#endif // _VMBUS_REG_H_
