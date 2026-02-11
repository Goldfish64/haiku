/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VMBusDevicePrivate.h"



VMBusDevice::VMBusDevice(device_node *node)
	:
	fNode(node),
	fStatus(B_NO_INIT),
	fChannelID(0),
	fDPCHandle(NULL),
	fIsOpen(false),
	fRingGPADL(0),
	fRingBuffer(NULL),
	fRingBufferLength(0),
	fTXRing(NULL),
	fTXRingLength(0),
	fRXRing(NULL),
	fRXRingLength(0),
	fCallback(NULL),
	fCallbackData(NULL),
	fVMBus(NULL),
	fVMBusCookie(NULL)
{
	CALLED();

	mutex_init(&fLock, "vmbus device lock");

	fStatus = gDeviceManager->get_attr_uint32(fNode, HYPERV_CHANNEL_ID_ITEM,
		&fChannelID, false);
	if (fStatus != B_OK) {
		ERROR("Failed to get channel ID\n");
		return;
	}

	device_node* parent = gDeviceManager->get_parent_node(node);
	gDeviceManager->get_driver(parent, (driver_module_info**)&fVMBus,
		(void**)&fVMBusCookie);
	gDeviceManager->put_node(parent);


}


VMBusDevice::~VMBusDevice()
{

}


status_t
VMBusDevice::Open(uint32 txLength, uint32 rxLength,
	hyperv_device_callback callback, void* callbackData)
{
	// Ring lengths must be page-aligned.
	if (txLength == 0 || rxLength == 0 || txLength != HV_PAGE_ALIGN(txLength)
		|| rxLength != HV_PAGE_ALIGN(rxLength))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&fLock);
	if (status != B_OK)
		return status;

	if (fIsOpen) {
		mutex_unlock(&fLock);
		return B_BUSY;
	}

	uint32 txTotalLength = txLength + HV_PAGE_SIZE;
	uint32 rxTotalLength = rxLength + HV_PAGE_SIZE;
	uint32 fRingBufferLength = txTotalLength + rxTotalLength;

	TRACE("Open channel %u tx length 0x%X rx length 0x%X\n", fChannelID,
		txLength, rxLength);

	// Create the GPADL used for the ring buffers
	status = fVMBus->allocate_gpadl(fVMBusCookie, fChannelID,
		fRingBufferLength, &fRingBuffer, &fRingGPADL);
	if (status != B_OK) {
		ERROR("Failed to allocate GPADL while opening channel %u (%s)\n",
			fChannelID, strerror(status));
		mutex_unlock(&fLock);
		return status;
	}

	fTXRing = reinterpret_cast<vmbus_ring_buffer*>(fRingBuffer);
	fTXRingLength = txLength;
	fRXRing = reinterpret_cast<vmbus_ring_buffer*>(static_cast<uint8*>(fRingBuffer)
		+ txTotalLength);
	fRXRingLength = rxLength;

	fCallback = callback;
	fCallbackData = callbackData;
	if (fCallback != NULL) {
		// Create callback DPC
		status = gDPC->new_dpc_queue(&fDPCHandle, "hyperv vmbusdev callback",
			B_NORMAL_PRIORITY);
		if (status != B_OK) {
			mutex_unlock(&fLock);
			return status;
		}
	}

	// Open the VMBus channel
	status = fVMBus->open_channel(fVMBusCookie, fChannelID, fRingGPADL,
		txTotalLength >> HV_PAGE_SHIFT,
		(hyperv_device_callback)((fCallback != NULL) ? _CallbackHandler : NULL),
		(fCallback != NULL) ? this : NULL);
	if (status != B_OK) {
		ERROR("Failed to open channel %u (%s)\n",
			fChannelID, strerror(status));
		mutex_unlock(&fLock);
		return status;
	}

	// Channel is now open, ready to go
	fIsOpen = true;
	mutex_unlock(&fLock);

	return B_OK;
}


status_t
VMBusDevice::Close()
{
	return B_OK;
}


/*static*/ void
VMBusDevice::_CallbackHandler(void* arg)
{
	VMBusDevice* vmbusDevice = reinterpret_cast<VMBusDevice*>(arg);
	gDPC->queue_dpc(vmbusDevice->fDPCHandle, _DPCHandler, arg);
}


/*static*/ void
VMBusDevice::_DPCHandler(void* arg)
{
	TRACE("CALLBACK\n");
	VMBusDevice* vmbusDevice = reinterpret_cast<VMBusDevice*>(arg);
	vmbusDevice->fCallback(vmbusDevice->fCallbackData);
}
