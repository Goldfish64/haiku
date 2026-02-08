/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_PRIVATE_H_
#define _HYPERV_PRIVATE_H_

#include <new>
#include <stdio.h>

#include <ACPI.h>
#include <dpc.h>

extern device_manager_info* gDeviceManager;
extern acpi_module_info* gACPI;
extern dpc_module_info* gDPC;

phys_addr_t hyperv_mem_vtophys(void* vaddr);

#define HYPERV_VTOPHYS_ERROR	(~0ULL)

#endif /* _HYPERV_PRIVATE_H_ */
