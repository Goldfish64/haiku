/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef HYPERV_PRIVATE_H
#define HYPERV_PRIVATE_H

#include <new>
#include <stdio.h>

#include <dpc.h>

extern device_manager_info* gDeviceManager;
extern dpc_module_info* gDPC;

phys_addr_t hyperv_mem_vtophys(void* vaddr);

#define HYPERV_VTOPHYS_ERROR	(~0ULL)

#endif
