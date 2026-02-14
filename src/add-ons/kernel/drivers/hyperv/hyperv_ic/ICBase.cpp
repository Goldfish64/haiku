/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "ICBase.h"

//#define TRACE_HYPERV_IC
#ifdef TRACE_HYPERV_IC
#	define TRACE(x...) dprintf("\33[94mhyperv_ic:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[94mhyperv_ic:\33[0m " x)
#define ERROR(x...)			dprintf("\33[94mhyperv_ic:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


static const uint32 sFrameworkVersions[] = {
	HV_IC_VERSION_V3,
	HV_IC_VERSION_2008
};

static const uint32 sFrameworkVersionsCount
	= sizeof(sFrameworkVersions) / sizeof(sFrameworkVersions[0]);


ICBase::ICBase(device_node* node)
	:
	fStatus(B_NO_INIT),
	fNode(node),
	fPacket(NULL),
	fHyperV(NULL),
	fHyperVCookie(NULL)
{
	CALLED();

	device_node* parent = gDeviceManager->get_parent_node(node);
	gDeviceManager->get_driver(parent, (driver_module_info**)&fHyperV,
		(void**)&fHyperVCookie);
	gDeviceManager->put_node(parent);

	fPacket = malloc(_GetPacketBufferLength());
	if (fPacket == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}
}


ICBase::~ICBase()
{
	CALLED();

	free(fPacket);
}


/*virtual*/ status_t
ICBase::_Connect(uint32 txLength, uint32 rxLength)
{
	CALLED();

	// Open the channel
	status_t status = fHyperV->open(fHyperVCookie, txLength, rxLength, _CallbackHandler, this);
	if (status != B_OK) {
		ERROR("Failed to open channel");
		return status;
	}

	return B_OK;
}


/*virtual*/ void
ICBase::_Disconnect()
{
	CALLED();
	fHyperV->close(fHyperVCookie);
}


void
ICBase::_HandleMessageSent(hv_ic_msg* icMessage)
{
}


status_t
ICBase::_NegotiateProtocol(hv_ic_msg_negotiate* message)
{
	CALLED();

	if (message->header.data_length < offsetof(hv_ic_msg_negotiate, versions[2])
			- sizeof(message->header)) {
		ERROR("IC[%u] invalid negotiate msg length 0x%X\n", _GetMessageType(),
			message->header.data_length);
		return B_BAD_VALUE;
	}

	if (message->framework_version_count == 0 || message->message_version_count == 0) {
		ERROR("IC[%u] invalid negotiate msg version count\n", _GetMessageType());
		return B_BAD_VALUE;
	}

	uint32 versionCount = message->framework_version_count + message->message_version_count;
	if (versionCount < 2) {
		ERROR("IC[%u] invalid negotiate msg version count\n", _GetMessageType());
		return B_BAD_VALUE;
	}

	// Match the highest supported framework version
	uint32 frameworkVersion;
	bool foundFrameworkVersion = false;
	for (uint32 i = 0; i < sFrameworkVersionsCount; i++) {
		for (uint32 j = 0; j < message->framework_version_count; j++) {
			TRACE("IC[%u] checking fw version %u.%u against %u.%u\n", _GetMessageType(),
				GET_IC_VERSION_MAJOR(sFrameworkVersions[i]), GET_IC_VERSION_MINOR(sFrameworkVersions[i]),
				GET_IC_VERSION_MAJOR(message->versions[j]), GET_IC_VERSION_MINOR(message->versions[j]));
			
			if (sFrameworkVersions[i] == message->versions[j]) {
				frameworkVersion = sFrameworkVersions[i];
				foundFrameworkVersion = true;
				break;
			}
		}

		if (foundFrameworkVersion)
			break;
	}

	uint32* messageVersions;
	uint32 messageVersionCount;
	_GetMessageVersions(&messageVersions, &messageVersionCount);

	// Match the highest supported message version
	uint32 messageVersion;
	bool foundMessageVersion = false;
	for (uint32 i = 0; i < messageVersionCount; i++) {
		for (uint32 j = message->message_version_count; j < versionCount; j++) {
			TRACE("IC[%u] checking msg version %u.%u against %u.%u\n", _GetMessageType(),
				GET_IC_VERSION_MAJOR(messageVersions[i]), GET_IC_VERSION_MINOR(messageVersions[i]),
				GET_IC_VERSION_MAJOR(message->versions[j]), GET_IC_VERSION_MINOR(message->versions[j]));
			
			if (messageVersions[i] == message->versions[j]) {
				messageVersion = messageVersions[i];
				foundMessageVersion = true;
				break;
			}
		}

		if (messageVersion)
			break;
	}

	if (!foundFrameworkVersion || !foundMessageVersion) {
		ERROR("IC%u unsupported versions\n", _GetMessageType());
		message->framework_version_count = 0;
		message->message_version_count = 0;
		return B_UNSUPPORTED;
	}

	TRACE("IC[%u] found supported fw version %u.%u msg version %u.%u\n",
		_GetMessageType(),
		GET_IC_VERSION_MAJOR(frameworkVersion), GET_IC_VERSION_MINOR(frameworkVersion),
		GET_IC_VERSION_MAJOR(messageVersion), GET_IC_VERSION_MINOR(messageVersion));

	message->framework_version_count = 1;
	message->message_version_count = 1;
	message->versions[0] = frameworkVersion;
	message->versions[1] = messageVersion;

	_HandleProtocolNegotiated(messageVersion);

	return B_OK;
}


/*static*/ void
ICBase::_CallbackHandler(void* arg)
{
	ICBase* icBaseDevice = reinterpret_cast<ICBase*>(arg);
	icBaseDevice->_Callback();
}


void
ICBase::_Callback()
{
	while (true) {
		vmbus_pkt_header header;
		uint32 headerLength = sizeof(header);
		uint32 packetLength = _GetPacketBufferLength();

		// Get the next received packet
		status_t status = fHyperV->read_packet(fHyperVCookie, &header, &headerLength,
			fPacket, &packetLength);
		if (status == B_DEV_NOT_READY) {
			break;
		} else if (status != B_OK) {
			ERROR("IC[%u] failed to read packet (%s)\n", _GetMessageType(), strerror(status));
			break;
		}

		if (packetLength < sizeof(hv_ic_msg_header)) {
			ERROR("IC[%u] invalid packet\n", _GetMessageType());
			continue;
		}
		
		// New IC packet received
		hv_ic_msg* message = reinterpret_cast<hv_ic_msg*>(fPacket);
		if (message->header.data_length <= packetLength - sizeof(message->header)) {
			if (message->header.type == HV_IC_MSGTYPE_NEGOTIATE) {
				// IC protocol negotiation
				status = _NegotiateProtocol(&message->negotiate);
				if (status != B_OK) {
					ERROR("IC[%u] protocol negotiation failed (%s)\n", _GetMessageType(),
						strerror(status));
					message->header.status = HV_IC_STATUS_FAILED;
				}
			} else if (message->header.type == _GetMessageType()) {
				// IC device-specific handling
				_HandleMessageReceived(message);
			} else {
				ERROR("IC[%u] unknown message type %u\n", _GetMessageType(), message->header.type);
				message->header.status = HV_IC_STATUS_FAILED;
			}
		} else {
			ERROR("IC[%u] invalid msg data length 0x%X pkt length 0x%X\n", _GetMessageType(),
				message->header.data_length, packetLength);
			message->header.status = HV_IC_STATUS_FAILED;
		}

		// Always respond to Hyper-V with the same packet that was originally received
		message->header.flags = HV_IC_FLAG_TRANSACTION | HV_IC_FLAG_RESPONSE;
		fHyperV->write_packet(fHyperVCookie, VMBUS_PKTTYPE_DATA_INBAND, fPacket,
			sizeof(hv_ic_msg_header) + message->header.data_length, false,
			header.transaction_id);

		// Callback for IC devices that need to be notified after the packet was sent
		_HandleMessageSent(message);
	}
}
