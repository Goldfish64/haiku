/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef VMBUS_DEVICE_PRIVATE_H
#define VMBUS_DEVICE_PRIVATE_H

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uuid.h>

#include <hyperv.h>
#include <hyperv_reg.h>
#include <vmbus_reg.h>

class VMBusDevice;

extern hyperv_device_interface gHyperVDeviceModule;

class VMBusDevice {
public:
									VMBusDevice(device_node *node);
									~VMBusDevice();
			status_t				InitCheck();

private:
};

#endif
