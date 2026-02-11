/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _VMBUS_DEVICE_PRIVATE_H_
#define _VMBUS_DEVICE_PRIVATE_H_

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lock.h>

#include <hyperv.h>
#include <hyperv_reg.h>
#include <vmbus_reg.h>

#include "HyperVPrivate.h"

#define TRACE_VMBUS_DEVICE
#ifdef TRACE_VMBUS_DEVICE
#	define TRACE(x...) dprintf("\33[36mvmbus_device:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[36mvmbus_device:\33[0m " x)
#define ERROR(x...)			dprintf("\33[36mvmbus_device:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

class VMBusDevice {
public:
									VMBusDevice(device_node *node);
									~VMBusDevice();
			status_t				InitCheck() const { return fStatus; }

			status_t				Open(uint32 txLength, uint32 rxLength,
										hyperv_device_callback callback, void* callbackData);
			status_t				Close();

private:
	static	void					_CallbackHandler(void* arg);
	static	void					_DPCHandler(void* arg);

private:
			device_node* 			fNode;
			status_t				fStatus;
			uint32					fChannelID;
			mutex					fLock;
			void*					fDPCHandle;
			bool					fIsOpen;

			uint32					fRingGPADL;
			void*					fRingBuffer;
			uint32					fRingBufferLength;
			vmbus_ring_buffer*		fTXRing;
			uint32					fTXRingLength;
			vmbus_ring_buffer*		fRXRing;
			uint32					fRXRingLength;

			hyperv_device_callback	fCallback;
			void*					fCallbackData;


			hyperv_bus_interface*	fVMBus;
			hyperv_bus				fVMBusCookie;
};

#endif // _VMBUS_DEVICE_PRIVATE_H_
