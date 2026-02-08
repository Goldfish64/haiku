/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef HV_VMBUS_H_
#define HV_VMBUS_H_

#include <device_manager.h>
#include <KernelExport.h>

#define VMBUS_MODULE_NAME 	"bus_managers/hyperv/root/driver_v1"
#define VMBUS_DEVICE_NAME	"bus_managers/hyperv/device/v1"

// Interface between VMBus and underlying ACPI device
typedef struct vmbus_bus_interface {
	driver_module_info info;

	uint8 (*get_irq)(void *cookie);
	status_t (*setup_interrupt)(void *cookie, interrupt_handler handler, void* data);
} vmbus_bus_interface;

#endif
