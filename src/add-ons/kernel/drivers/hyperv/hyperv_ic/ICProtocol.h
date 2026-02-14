/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_IC_PROTOCOL_H_
#define _HYPERV_IC_PROTOCOL_H_


#define HV_IC_PKTBUFFER_SIZE		128

#define MAKE_IC_VERSION(major, minor)	((((minor) << 16) & 0xFFFF0000) | ((major) & 0x0000FFFF))
#define GET_IC_VERSION_MAJOR(version)	((version) & 0xFFFF)
#define GET_IC_VERSION_MINOR(version)	(((version) >> 16) & 0xFFFF)

// IC framework versions
#define HV_IC_VERSION_2008		MAKE_IC_VERSION(1, 0)
#define HV_IC_VERSION_V3		MAKE_IC_VERSION(3, 0)


enum { // IC message types
	HV_IC_MSGTYPE_NEGOTIATE		= 0,
	HV_IC_MSGTYPE_HEARTBEAT		= 1,
	HV_IC_MSGTYPE_KVP			= 2,
	HV_IC_MSGTYPE_SHUTDOWN		= 3,
	HV_IC_MSGTYPE_TIMESYNC		= 4,
	HV_IC_MSGTYPE_VSS			= 5,
	HV_IC_MSGTYPE_FILECOPY		= 7
};


// IC message flags
#define HV_IC_FLAG_TRANSACTION		(1 << 0)
#define HV_IC_FLAG_REQUEST			(1 << 1)
#define HV_IC_FLAG_RESPONSE			(1 << 2)


enum { // IC status
	HV_IC_STATUS_OK					= 0x0,
	HV_IC_STATUS_FAILED				= 0x80004005,
	HV_IC_STATUS_TIMEOUT			= 0x800705B4,
	HV_IC_STATUS_INVALID_ARG		= 0x80070057,
	HV_IC_STATUS_ALREADY_EXISTS		= 0x80070050,
	HV_IC_STATUS_DISK_FULL			= 0x80070070
};


typedef struct { // IC message header
	uint32	pipe_flags;
	uint32	pipe_messages;
	uint32	framework_version;
	uint16	type;
	uint32	message_version;
	uint16	data_length;
	uint32	status;
	uint8	transaction_id;
	uint8	flags;
	uint16	reserved;
} _PACKED hv_ic_msg_header;


typedef struct { // IC negotiation message
	hv_ic_msg_header	header;

	uint16	framework_version_count;
	uint16	message_version_count;
	uint32	reserved;
	uint32	versions[];
} _PACKED hv_ic_msg_negotiate;


typedef struct { // IC combined message
	union {
		hv_ic_msg_header		header;
		hv_ic_msg_negotiate		negotiate;
	};
} _PACKED hv_ic_msg;


#endif // _HYPERV_IC_PROTOCOL_H_
