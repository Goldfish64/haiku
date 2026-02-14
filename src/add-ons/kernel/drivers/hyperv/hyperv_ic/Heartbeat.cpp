/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Heartbeat.h"

static uint32 sHeartbeatVersions[] = {
	HV_HEARTBEAT_VERSION_V3,
	HV_HEARTBEAT_VERSION_V1
};


Heartbeat::Heartbeat(device_node* node)
	: ICBase(node)
{
	CALLED();

	// Connect to the heartbeat device
	fStatus = _Connect(HV_HEARTBEAT_RING_SIZE, HV_HEARTBEAT_RING_SIZE);
}


Heartbeat::~Heartbeat()
{
	CALLED();
	_Disconnect();
}


void
Heartbeat::_GetMessageVersions(uint32* _versions[], uint32* _versionCount) const
{
	CALLED();
	*_versions = sHeartbeatVersions;
	*_versionCount = sizeof(sHeartbeatVersions) / sizeof(sHeartbeatVersions[0]);
}


void
Heartbeat::_HandleProtocolNegotiated(uint32 version)
{
	TRACE("Heartbeat protocol: 0x%X\n", version);
}


void
Heartbeat::_HandleMessageReceived(hv_ic_msg* icMessage)
{
	hv_heartbeat_msg* message = reinterpret_cast<hv_heartbeat_msg*>(icMessage);
	if (message->header.data_length < offsetof(hv_heartbeat_msg, heartbeat.sequence)
			- sizeof(message->header)) {
		ERROR("Heartbeat msg invalid length 0x%X\n", message->header.data_length);
		message->header.status = HV_IC_STATUS_FAILED;
		return;
	}

	TRACE("New heartbeat msg, sequence %llu\n",
		(unsigned long long)message->heartbeat.sequence);
	message->heartbeat.sequence++;
}
