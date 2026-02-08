/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VMBusDevicePrivate.h"


#define TRACE_VMBUS_DEVICE
#ifdef TRACE_VMBUS_DEVICE
#	define TRACE(x...) dprintf("\33[36mvmbus_device:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[36mvmbus_device:\33[0m " x)
#define ERROR(x...)			dprintf("\33[36mvmbus_device:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

static status_t
vmbus_device_init(device_node* node, void** _device)
{
	CALLED();

	return B_OK;
}


static void
vmbus_device_uninit(void* _device)
{
	CALLED();
}


static void
vmbus_device_removed(void* _device)
{
	CALLED();
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


hyperv_device_interface gHyperVDeviceModule = {
	{
		{
			HYPERV_DEVICE_MODULE_NAME,
			0,
			std_ops
		},
		NULL,	// supported devices
		NULL,	// register node
		vmbus_device_init,
		vmbus_device_uninit,
		NULL,	// register child devices
		NULL,	// rescan
		vmbus_device_removed
	}
};
