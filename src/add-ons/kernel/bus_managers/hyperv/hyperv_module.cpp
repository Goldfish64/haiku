/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <vmbus.h>
#include "hyperv_private.h"
#include "VMBusPrivate.h"

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
hyperv_detect()
{
	CALLED();

	// Check for hypervisor.
	cpu_ent *cpu = get_cpu_struct();
	if ((cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) == 0) {
		TRACE("No hypervisor detected\n");
		return B_ERROR;
	}

	// Check for Hyper-V CPUID leaves.
	cpuid_info cpuInfo;
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HYPERVISOR, 0);
	if (cpuInfo.regs.eax < IA32_CPUID_LEAF_HV_IMP_LIMITS) {
		TRACE("Not running on Hyper-V\n");
		return B_ERROR;
	}

	// Check for Hyper-V signature.
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HV_INT_ID, 0);
	if (cpuInfo.regs.eax != HV_CPUID_INTERFACE_ID) {
		TRACE("Not running on Hyper-V\n");
		return B_ERROR;
	}

#ifdef TRACE_HYPERV
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HV_SYS_ID, 0);
	TRACE("Hyper-V version: %d.%d.%d [SP%d]\n", cpuInfo.regs.ebx >> 16, cpuInfo.regs.ebx & 0xFFFF,
		cpuInfo.regs.eax, cpuInfo.regs.ecx);
#endif

	return B_OK;
}

static status_t
vmbus_init(device_node* node, void** _device)
{
	CALLED();
	status_t status = hyperv_detect();
	if (status != B_OK) {
		ERROR("System is not Hyper-V\n");
		return status;
	}

	VMBus* device = new(std::nothrow) VMBus(node);
	if (device == NULL) {
		ERROR("Unable to allocate VMBus\n");
		return B_NO_MEMORY;
	}

	status = device->InitCheck();
	if (status != B_OK) {
		ERROR("failed to set up VMBus device object\n");
		delete device;
		return status;
	}
	TRACE("VMBus object created\n");

	*_device = device;
	return B_OK;
}


static void
vmbus_uninit(void* _device)
{
	CALLED();
	VMBus* device = (VMBus*)_device;
	delete device;
}


static void
vmbus_removed(void* _device)
{
	CALLED();
}


static status_t
vmbus_added_device(device_node* parent)
{
	CALLED();

	device_attr attributes[] = {
		{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "hyperv"}},
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "Hyper-V VMBus root"}},
		{ NULL }
	};

	return gDeviceManager->register_node(parent, VMBUS_DEVICE_NAME,
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

static driver_module_info sVMBusDeviceModule = {
	{
		VMBUS_DEVICE_NAME,
		0,
		std_ops
	},
	NULL, // supported devices
	NULL, // register node
	vmbus_init,
	vmbus_uninit,
	NULL, // register child devices
	NULL, // rescan
	vmbus_removed,
	NULL, // suspend
	NULL // resume
};

// Root device that binds to the ACPI or PCI bus. It will register an mmc_bus_interface
// node for each SD slot in the device.
static driver_module_info sVMBusModule = {
	{
		VMBUS_MODULE_NAME,
		0,
		&std_ops
	},
	NULL,
	vmbus_added_device,
	NULL,
	NULL,	// uninit
	NULL,
	NULL,	// rescan
	NULL,	// device removed
};

module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_DPC_MODULE_NAME, (module_info **)&gDPC },
	{}
};

module_info* modules[] = {
	(module_info* )&sVMBusModule,
	(module_info* )&sVMBusDeviceModule,
	NULL
};
