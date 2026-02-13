/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Driver.h"
#include "HIDDevice.h"
#include "HIDReport.h"
#include "HIDReportItem.h"
#include "HIDWriter.h"
#include "ProtocolHandler.h"


HIDDevice::HIDDevice(hyperv_device_interface* hyperv,
	hyperv_device hyperv_cookie)
	:
	fStatus(B_NO_INIT),
	fOpenCount(0),
	fRemoved(false),
	fParser(this),
	fProtocolHandlerCount(0),
	fProtocolHandlerList(NULL),
	fHyperV(hyperv),
	fHyperVCookie(hyperv_cookie),
	fDeviceInfo(NULL),
	fDeviceInfoLength(0),
	fPacket(NULL)
{
	CALLED();

	fProtocolRespEvent.Init(this, "hyper-v hid protoresp");
	fDeviceInfoEvent.Init(this, "hyper-v hid devinfo");

	fPacket = malloc(HV_HID_RX_PKT_BUFFER_SIZE);
	if (fPacket == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	// Connect to the Hyper-V HID device
	fStatus = _Connect();
	if (fStatus != B_OK) {
		ERROR("Failed to connect to Hyper-V HID\n");
		return;
	}

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList,
		fProtocolHandlerCount);
}


HIDDevice::~HIDDevice()
{
	CALLED();

	ProtocolHandler* handler = fProtocolHandlerList;
	while (handler != NULL) {
		ProtocolHandler *next = handler->NextHandler();
		delete handler;
		handler = next;
	}

	free(fPacket);
}


status_t
HIDDevice::Open(ProtocolHandler* handler, uint32 flags)
{
	atomic_add(&fOpenCount, 1);
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler* handler)
{
	atomic_add(&fOpenCount, -1);
	return B_OK;
}


void
HIDDevice::Removed()
{
	fRemoved = true;
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport* report)
{
	if (fRemoved)
		return ENODEV;

	// Reports get sent as they become available, cannot query on-demand
	return B_OK;
}


status_t
HIDDevice::SendReport(HIDReport* report)
{
	// Hyper-V does not support reports to itself
	return B_NOT_SUPPORTED;
}


ProtocolHandler*
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler* handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}


/*static*/ void
HIDDevice::_CallbackHandler(void* data)
{
	HIDDevice* hidDevice = reinterpret_cast<HIDDevice*>(data);
	hidDevice->_Callback();
}


void
HIDDevice::_Callback()
{
	while (true) {
		vmbus_pkt_header header;
		uint32 headerLength = sizeof(header);
		uint32 packetLength = HV_HID_RX_PKT_BUFFER_SIZE;

		status_t status = fHyperV->read_packet(fHyperVCookie, &header, &headerLength,
			fPacket, &packetLength);
		if (status == B_DEV_NOT_READY) {
			break;
		} else if (status != B_OK) {
			ERROR("Failed to read packet (%s)\n", strerror(status));
			break;
		}

		// Check if this is an HID pipe data message
		hv_hid_pipe_in_msg* message = reinterpret_cast<hv_hid_pipe_in_msg*>(fPacket);
		if (message->pipe_header.type != HV_HID_PIPE_MSGTYPE_DATA) {
			ERROR("Non-data HID pipe message type %u received\n", message->pipe_header.type);
			continue;
		}

		switch (message->header.type) {
			case HV_HID_MSGTYPE_PROTOCOL_RESPONSE:
				memcpy(&fProtocolResponse, &message->protocol_resp, sizeof(fProtocolResponse));
				fProtocolRespEvent.NotifyAll();
				break;

			case HV_HID_MSGTYPE_INITIAL_DEV_INFO:
				if (fDeviceInfo != NULL) {
					free(fDeviceInfo);
				}
				fDeviceInfo = reinterpret_cast<hv_hid_msg_initial_dev_info*>(malloc(message->header.length));
				if (fDeviceInfo != NULL) {
					fDeviceInfoLength = message->header.length;
					memcpy(fDeviceInfo, &message->dev_info, fDeviceInfoLength);
					fDeviceInfoEvent.NotifyAll();
				} else {
					fDeviceInfoLength = 0;
					ERROR("Failed to allocate device info\n");
					fDeviceInfoEvent.NotifyAll(B_NO_MEMORY);
				}
				break;

			case HV_HID_MSGTYPE_INPUT_REPORT:
				TRACE("New HID input report\n");
				fParser.SetReport(B_OK, message->input_report.data, message->input_report.header.length);
				break;

			default:
				TRACE("Unexpected HID message type %u received\n", message->header.type);
				break;
		}
	}
}


status_t
HIDDevice::_Connect()
{
	// Open the channel
	status_t status = fHyperV->open(fHyperVCookie, HV_HID_RING_SIZE, HV_HID_RING_SIZE,
		_CallbackHandler, this);
	if (status != B_OK) {
		ERROR("Failed to open channel");
		return status;
	}

	// Build the protocol request message
	hv_hid_pipe_out_msg message;
	message.pipe_header.type = HV_HID_PIPE_MSGTYPE_DATA;
	message.pipe_header.length = sizeof(message.protocol_req);
	message.header.type = HV_HID_MSGTYPE_PROTOCOL_REQUEST;
	message.header.length = message.pipe_header.length - sizeof(message.header);
	message.protocol_req.version = HV_HID_VERSION_V2_0;

	ConditionVariableEntry protocolRespEntry;
	ConditionVariableEntry deviceInfoEntry;
	fProtocolRespEvent.Add(&protocolRespEntry);
	fDeviceInfoEvent.Add(&deviceInfoEntry);

	// Send the protocol request message
	status = fHyperV->write_packet(fHyperVCookie, VMBUS_PKTTYPE_DATA_INBAND, &message,
		sizeof(message.pipe_header) + message.pipe_header.length, TRUE,
		HV_HID_REQUEST_TRANS_ID);
	if (status != B_OK) {
		ERROR("Failed to send HID protocol request");
		return status;
	}

	// Wait for the protocol response to be received
	status = protocolRespEntry.Wait(B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, HV_HID_TIMEOUT_US);
	if (status != B_OK)
		return status;

	TRACE("HID protocol version %u.%u status %u\n",
		GET_HID_VERSION_MAJOR(fProtocolResponse.version),
		GET_HID_VERSION_MINOR(fProtocolResponse.version),
		fProtocolResponse.result);

	// Wait for the initial device info to be received
	status = deviceInfoEntry.Wait(B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, HV_HID_TIMEOUT_US);
	if (status != B_OK)
		return status;

	message.pipe_header.type = HV_HID_PIPE_MSGTYPE_DATA;
	message.pipe_header.length = sizeof(message.dev_info_ack);
	message.header.type = HV_HID_MSGTYPE_INITIAL_DEV_INFO_ACK;
	message.header.length = message.pipe_header.length - sizeof(message.header);
	message.dev_info_ack.reserved = 0;

	// Send device info acknowledgement to Hyper-V
	status = fHyperV->write_packet(fHyperVCookie, VMBUS_PKTTYPE_DATA_INBAND, &message,
		sizeof(message.pipe_header) + message.pipe_header.length, FALSE,
		HV_HID_REQUEST_TRANS_ID);
	if (status != B_OK) {
		ERROR("Failed to send HID device info ack");
		return status;
	}

	TRACE("Hyper-V HID vid 0x%04X pid 0x%04X version 0x%X\n",
		fDeviceInfo->info.vendor_id, fDeviceInfo->info.product_id, fDeviceInfo->info.version);

	// Send the HID descriptor
	status = fParser.ParseReportDescriptor(fDeviceInfo->descriptor_data,
		fDeviceInfo->descriptor.hid_descriptor_length);
	free(fDeviceInfo);
	fDeviceInfo = NULL;

	return status;
}
