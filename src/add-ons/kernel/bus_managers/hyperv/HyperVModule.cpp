/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include "HyperVPrivate.h"

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


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_ACPI_MODULE_NAME, (module_info**)&gACPI },
	{ B_DPC_MODULE_NAME, (module_info **)&gDPC },
	{}
};


module_info* modules[] = {
	(module_info* )&gVMBusModule,
	(module_info* )&gVMBusDeviceModule,
	NULL
};
