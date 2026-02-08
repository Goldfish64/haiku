/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <hyperv.h>

#include "HyperVPrivate.h"
#include "VMBusPrivate.h"
#include "VMBusDevicePrivate.h"

#define TRACE_HYPERV
#ifdef TRACE_HYPERV
#	define TRACE(x...) dprintf("\33[33mhyperv:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mhyperv:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33mhyperv:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

device_manager_info* gDeviceManager;
acpi_module_info* gACPI;
dpc_module_info* gDPC;

phys_addr_t
hyperv_mem_vtophys(void* vaddr)
{
	physical_entry entry;
	status_t status = get_memory_map((void*)vaddr, 1, &entry, 1);
	if (status != B_OK) {
		panic("hyperv: get_memory_map failed for %p: %s\n",
			(void*)vaddr, strerror(status));
		return HYPERV_VTOPHYS_ERROR;
	}

	return entry.address;
}


static status_t
hyperv_added_device(device_node* parent)
{
	CALLED();
	device_attr attributes[] = {
		{ B_DEVICE_BUS, B_STRING_TYPE, { .string = HYPERV_BUS_NAME }},
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "Hyper-V VMBus root" }},
		{ NULL }
	};

	return gDeviceManager->register_node(parent, HYPERV_BUS_MODULE_NAME,
		attributes, NULL, NULL);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			// Nothing to do
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}

driver_module_info gHyperVControllerModule = {
	{
		HYPERV_CONTROLLER_MODULE_NAME,
		0,
		&std_ops
	},
	NULL, // supported devices
	hyperv_added_device,
	NULL,
	NULL,
	NULL
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_ACPI_MODULE_NAME, (module_info**)&gACPI },
	{ B_DPC_MODULE_NAME, (module_info **)&gDPC },
	{}
};


module_info* modules[] = {
	//(module_info* )&gHyperVControllerModule,
	(module_info* )&gVMBusModule,
	(module_info* )&gHyperVDeviceModule,
	NULL
};
