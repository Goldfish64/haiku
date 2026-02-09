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

// Device pretty names
#define HYPERV_PRETTYNAME_VMBUS				"Hyper-V Virtual Machine Bus"
#define HYPERV_PRETTYNAME_AVMA				"Hyper-V Automatic Virtual Machine Activation"
#define HYPERV_PRETTYNAME_BALLOON			"Hyper-V Dynamic Memory"
#define HYPERV_PRETTYNAME_DISPLAY			"Hyper-V Display"
#define HYPERV_PRETTYNAME_FIBRECHANNEL		"Hyper-V Fibre Channel"
#define HYPERV_PRETTYNAME_FILECOPY			"Hyper-V File Copy"
#define HYPERV_PRETTYNAME_HEARTBEAT			"Hyper-V Heartbeat"
#define HYPERV_PRETTYNAME_IDE				"Hyper-V IDE Accelerator"
#define HYPERV_PRETTYNAME_KEYBOARD			"Hyper-V Keyboard"
#define HYPERV_PRETTYNAME_KVP				"Hyper-V Data Exchange"
#define HYPERV_PRETTYNAME_MOUSE				"Hyper-V Mouse"
#define HYPERV_PRETTYNAME_NETWORK			"Hyper-V Network Adapter"
#define HYPERV_PRETTYNAME_PCI				"Hyper-V PCI Bridge"
#define HYPERV_PRETTYNAME_RDCONTROL			"Hyper-V Remote Desktop Control"
#define HYPERV_PRETTYNAME_RDMA				"Hyper-V RDMA"
#define HYPERV_PRETTYNAME_RDVIRT			"Hyper-V Remote Desktop Virtualization"
#define HYPERV_PRETTYNAME_SCSI				"Hyper-V SCSI Adapter"
#define HYPERV_PRETTYNAME_SHUTDOWN			"Hyper-V Guest Shutdown"
#define HYPERV_PRETTYNAME_TIMESYNC			"Hyper-V Time Synchronization"
#define HYPERV_PRETTYNAME_UNKNOWN			"Hyper-V Unknown Device"
#define HYPERV_PRETTYNAME_VSS				"Hyper-V Volume Shadow Copy"


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
