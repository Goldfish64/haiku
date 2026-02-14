/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_H_
#define _HYPERV_H_

#include <device_manager.h>
#include <KernelExport.h>

#include <hyperv_reg.h>
#include <vmbus_reg.h>

#define HYPERV_VMBUS_MODULE_NAME		"bus_managers/hyperv/root/driver_v1"
#define HYPERV_DEVICE_MODULE_NAME		"bus_managers/hyperv/device/v1"

#define HYPERV_BUS_NAME					"hyperv"

// Device pretty names
#define HYPERV_PRETTYNAME_VMBUS				"Hyper-V Virtual Machine Bus"
#define HYPERV_PRETTYNAME_VMBUS_DEVICE_FMT	"Hyper-V Channel %u"
#define HYPERV_PRETTYNAME_AVMA				"Hyper-V Automatic Virtual Machine Activation"
#define HYPERV_PRETTYNAME_BALLOON			"Hyper-V Dynamic Memory"
#define HYPERV_PRETTYNAME_DISPLAY			"Hyper-V Display"
#define HYPERV_PRETTYNAME_FIBRECHANNEL		"Hyper-V Fibre Channel"
#define HYPERV_PRETTYNAME_FILECOPY			"Hyper-V File Copy"
#define HYPERV_PRETTYNAME_HEARTBEAT			"Hyper-V Heartbeat"
#define HYPERV_PRETTYNAME_IDE				"Hyper-V IDE Accelerator"
#define HYPERV_PRETTYNAME_INPUT				"Hyper-V Input"
#define HYPERV_PRETTYNAME_KEYBOARD			"Hyper-V Keyboard"
#define HYPERV_PRETTYNAME_KVP				"Hyper-V Data Exchange"
#define HYPERV_PRETTYNAME_NETWORK			"Hyper-V Network Adapter"
#define HYPERV_PRETTYNAME_PCI				"Hyper-V PCI Bridge"
#define HYPERV_PRETTYNAME_RDCONTROL			"Hyper-V Remote Desktop Control"
#define HYPERV_PRETTYNAME_RDMA				"Hyper-V RDMA"
#define HYPERV_PRETTYNAME_RDVIRT			"Hyper-V Remote Desktop Virtualization"
#define HYPERV_PRETTYNAME_SCSI				"Hyper-V SCSI Adapter"
#define HYPERV_PRETTYNAME_SHUTDOWN			"Hyper-V Guest Shutdown"
#define HYPERV_PRETTYNAME_TIMESYNC			"Hyper-V Time Synchronization"
#define HYPERV_PRETTYNAME_VSS				"Hyper-V Volume Shadow Copy"

typedef void* hyperv_bus;
typedef void* hyperv_device;

typedef void (*hyperv_bus_callback)(void* data);
typedef void (*hyperv_device_callback)(void* data);

// Interface between the VMBus bus device driver, and the VMBus bus manager
typedef struct hyperv_bus_interface {
	driver_module_info info;

	status_t (*open_channel)(hyperv_bus cookie, uint32 channel, uint32 gpadl,
		uint32 rxOffset, hyperv_bus_callback callback, void* callbackData);
	status_t (*close_channel)(hyperv_bus cookie, uint32 channel);
	status_t (*allocate_gpadl)(hyperv_bus cookie, uint32 channel, uint32 length,
		void** _buffer, uint32* _gpadl);
	status_t (*free_gpadl)(hyperv_bus cookie, uint32 channel, uint32 gpadl);
	status_t (*signal_channel)(hyperv_bus cookie, uint32 channel);
} hyperv_bus_interface;

// Interface between the VMBus device driver, and the VMBus device bus manager
typedef struct hyperv_device_interface {
	driver_module_info info;

	status_t (*open)(hyperv_device cookie, uint32 txLength, uint32 rxLength,
		hyperv_device_callback callback, void* callbackData);
	void (*close)(hyperv_device cookie);
	status_t (*write_packet)(hyperv_device cookie, uint16 type, void* buffer,
		uint32 length, bool responseRequired, uint64 transactionID);
	status_t (*read_packet)(hyperv_device cookie, vmbus_pkt_header* _header,
		uint32* _headerLength, void* _buffer, uint32* _length);
} hyperv_device_interface;

// Device attributes for the VMBus device

// Channel ID
#define HYPERV_CHANNEL_ID_ITEM		"hyperv/channel"
// Device type UUID
#define HYPERV_DEVICE_TYPE_ITEM		"hyperv/type"
// Instance UUID
#define HYPERV_INSTANCE_ID_ITEM		"hyperv/instance"

#endif // _HYPERV_H_
