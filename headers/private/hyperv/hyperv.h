/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_H_
#define _HYPERV_H_

#include <device_manager.h>
#include <KernelExport.h>

#define HYPERV_VMBUS_MODULE_NAME		"bus_managers/hyperv/root/driver_v1"
#define HYPERV_DEVICE_MODULE_NAME		"bus_managers/hyperv/device/driver_v1"

#define HYPERV_BUS_NAME					"hyperv"


// Interface between the VMBus device driver, and the VMBus
typedef struct hyperv_device_interface {
	driver_module_info info;
	
} hyperv_device_interface;

// Device attributes for the VMBus device

// Channel ID
#define HYPERV_CHANNEL_ID_ITEM		"hyperv/channel"
// Device type UUID
#define HYPERV_DEVICE_TYPE_ITEM		"hyperv/type"
// Instance UUID
#define HYPERV_INSTANCE_ID_ITEM		"hyperv/instance"

#endif /* _HYPERV_H_ */
