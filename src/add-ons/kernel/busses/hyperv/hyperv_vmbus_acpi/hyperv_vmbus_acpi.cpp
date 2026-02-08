/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ACPI.h>
#include "acpi.h"

#include <vmbus.h>
#include "hyperv_vmbus_acpi.h"

#define TRACE_VMBUS_ACPI
#ifdef TRACE_VMBUS_ACPI
#	define TRACE(x...) dprintf("\33[36mhyperv_vmbus_acpi:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[36mhyperv_vmbus_acpi:\33[0m " x)
#define ERROR(x...)			dprintf("\33[36mhyperv_vmbus_acpi:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#define VMBUS_ACPI_DEVICE_MODULE_NAME	"busses/hyperv/hyperv_vmbus_acpi/driver_v1"
#define VMBUS_ACPI_BUS_MODULE_NAME	"busses/hyperv/hyperv_vmbus_acpi/device/v1"

device_manager_info* gDeviceManager;
driver_module_info* gVMBus;

static acpi_status
vmbus_acpi_scan_parse_callback(ACPI_RESOURCE* res, void* context)
{
	vmbus_acpi_crs* crs = (vmbus_acpi_crs*)context;

	// Grab the first IRQ only. Gen1 usually has two IRQs, Gen2 just one.
	// Only one IRQ is required for the VMBus device.
	if (res->Type == ACPI_RESOURCE_TYPE_IRQ && crs->irq == 0) {
		crs->irq = res->Data.Irq.Interrupt;
		crs->irq_triggering = res->Data.Irq.Triggering;
		crs->irq_polarity = res->Data.Irq.Polarity;
		crs->irq_shareable = res->Data.Irq.Shareable;
	}

	return B_OK;
}


static status_t
init_bus(device_node* node, void** bus_cookie)
{
	CALLED();
	vmbus_acpi_info* bus = new(std::nothrow) vmbus_acpi_info;
	if (bus == NULL)
		return B_NO_MEMORY;
	memset(bus, 0, sizeof(vmbus_acpi_info));

	// Get the ACPI driver and device
	acpi_device_module_info* acpi;
	acpi_device device;

	device_node* parent = gDeviceManager->get_parent_node(node);
	device_node* acpiParent = gDeviceManager->get_parent_node(parent);
	gDeviceManager->get_driver(acpiParent, (driver_module_info**)&acpi,
			(void**)&device);
	gDeviceManager->put_node(acpiParent);
	gDeviceManager->put_node(parent);

	if(acpi->walk_resources(device, (ACPI_STRING)"_CRS",
			vmbus_acpi_scan_parse_callback, &bus->crs) != B_OK) {
		ERROR("Couldn't scan ACPI register set\n");
		return B_IO_ERROR;
	}

	if (bus->crs.irq == 0) {
		ERROR("No irq\n");
		return B_IO_ERROR;
	}
	TRACE("irq interrupt line: %u\n", bus->crs.irq);

	*bus_cookie = bus;
	return B_OK;
}


static uint8
vmbus_acpi_get_irq(void* cookie)
{
	CALLED();
	vmbus_acpi_info* bus = (vmbus_acpi_info*) cookie;
	return bus->crs.irq;
}


static status_t
vmbus_acpi_setup_interrupt(void* cookie, interrupt_handler handler, void* data)
{
	CALLED();
	vmbus_acpi_info* bus = (vmbus_acpi_info*) cookie;
	status_t status = install_io_interrupt_handler(bus->crs.irq,
		handler, data, 0);
	if (status != B_OK) {
		ERROR("Can't install interrupt handler\n");
		return status;
	}

	return B_OK;
}


static status_t
register_child_devices(void* cookie)
{
	CALLED();
	device_node* node = (device_node*)cookie;
	device_attr attributes[] = {
		// Properties of this controller for vmbus bus manager
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "Hyper-V VMBus" }},
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE,
			{ .string = VMBUS_MODULE_NAME }},
		{ NULL }
	};

	return gDeviceManager->register_node(node, VMBUS_ACPI_BUS_MODULE_NAME,
		attributes, NULL, NULL);
}

static status_t
init_device(device_node* node, void** device_cookie)
{
	CALLED();
	*device_cookie = node;
	return B_OK;
}

static status_t
register_device(device_node* parent)
{
	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Hyper-V VMBus ACPI"}},
		{}
	};

	return gDeviceManager->register_node(parent,
		VMBUS_ACPI_DEVICE_MODULE_NAME, attrs, NULL, NULL);
}


static float
supports_device(device_node* parent)
{
	CALLED();
	const char* bus;
    const char* hid;
	const char* path;
	uint32 type;

	// Ensure parent is an ACPI device node.
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK) {
		TRACE("Could not find required attribute device/bus\n");
		return -1;
	}

	if (strcmp(bus, "acpi") != 0) {
		return 0.0f;
	}

    if (gDeviceManager->get_attr_uint32(parent, ACPI_DEVICE_TYPE_ITEM, &type, false) != B_OK
		|| type != ACPI_TYPE_DEVICE) {
		return 0.0f;
	}

	// Check if the HID indicates this is the VMBus ACPI device.
	if (gDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &hid, false) != B_OK
		|| strcmp(hid, "VMBUS") != 0) {
		return 0.0f;
	}

	if (gDeviceManager->get_attr_string(parent, ACPI_DEVICE_PATH_ITEM, &path, false) != B_OK) {
		return 0.0f;
	}

	// Check if the path indicates this is the VMBus ACPI device.
	// Gen1 VMs may have both VMB8 and VMBS devices, only want to bind to one.
	const char* name = strstr(path, "VMBS");
	if (name == NULL || strcmp(name, "VMBS") != 0) {
		return 0.0f;
	}

	TRACE("Hyper-V VMBus ACPI device found! path %s\n", path);
	return 0.8f;
}

module_dependency module_dependencies[] = {
	{ VMBUS_MODULE_NAME, (module_info**)&gVMBus },
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{}
};

static vmbus_bus_interface sVMBusACPIDeviceModule = {
	{
		{
			VMBUS_ACPI_BUS_MODULE_NAME,
			0,
			NULL
		},
		NULL,	// supports device
		NULL,	// register device
		init_bus,
		NULL,	// uninit
		NULL,	// register child devices
		NULL,	// rescan
		NULL,	// device removed
	},

	vmbus_acpi_get_irq,
	vmbus_acpi_setup_interrupt
};

// Root device that binds to the ACPI or PCI bus. It will register an mmc_bus_interface
// node for each SD slot in the device.
static driver_module_info sVMBusDevice = {
	{
		VMBUS_ACPI_DEVICE_MODULE_NAME,
		0,
		NULL
	},
	supports_device,
	register_device,
	init_device,
	NULL,	// uninit
	register_child_devices,
	NULL,	// rescan
	NULL,	// device removed
};

module_info* modules[] = {
	(module_info* )&sVMBusDevice,
	(module_info* )&sVMBusACPIDeviceModule,
	NULL
};
