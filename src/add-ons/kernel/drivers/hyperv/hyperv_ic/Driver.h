/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_IC_DRIVER_H_
#define _HYPERV_IC_DRIVER_H_

#include <new>
#include <stdio.h>

#include <device_manager.h>
#include <KernelExport.h>

#include <hyperv.h>

extern device_manager_info* gDeviceManager;

extern driver_module_info gICHeartbeatDriverModule;


#endif // _HYPERV_IC_DRIVER_H_
