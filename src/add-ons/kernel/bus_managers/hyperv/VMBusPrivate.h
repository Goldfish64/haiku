/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _VMBUS_PRIVATE_H_
#define _VMBUS_PRIVATE_H_

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include "acpi.h"
#include <arch_cpu.h>
#include <condition_variable.h>
#include <cpu.h>
#include <dpc.h>
#include <lock.h>
#include <smp.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>

#include <hyperv.h>
#include <hyperv_reg.h>
#include <vmbus_reg.h>

#include "Driver.h"

//#define TRACE_VMBUS
#ifdef TRACE_VMBUS
#	define TRACE(x...) dprintf("\33[35mvmbus:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[35mvmbus:\33[0m " x)
#define ERROR(x...)			dprintf("\33[35mvmbus:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

status_t vmbus_detect_hyperv();

class VMBus;

typedef struct {
	VMBus*							vmbus;
	int32_t							cpu;
	volatile hv_message_page*		messages;
	hv_event_flags_page*			event_flags;
} VMBusPerCPUInfo;


 // VMBus message info used for transactions
struct VMBusMsgInfo : DoublyLinkedListLinkImpl<VMBusMsgInfo> {
	hypercall_post_msg_input	post_msg;
	phys_addr_t					post_msg_physaddr;
	vmbus_msg*					message;

	uint32						resp_type;
	uint32						resp_data;
	ConditionVariable			condition_variable;
};
typedef DoublyLinkedList<VMBusMsgInfo> VMBusMsgInfoList;


// Channel GPADL list
struct VMBusGPADLInfo : DoublyLinkedListLinkImpl<VMBusGPADLInfo> {
	uint32	gpadl_id;
	uint32	length;
	area_id areaid;
};
typedef DoublyLinkedList<VMBusGPADLInfo> VMBusGPADLInfoList;


// Active channel info
struct VMBusChannelInfo : DoublyLinkedListLinkImpl<VMBusChannelInfo> {
	uint32_t			channel_id;
	vmbus_guid_t		type_id;
	vmbus_guid_t		instance_id;
	bool				dedicated_int;
	uint32_t			connection_id;

	VMBus*				vmbus;
	mutex				lock;
	device_node*		node;
	VMBusGPADLInfoList	gpadls;
	hyperv_bus_callback	callback;
	void*				callback_data;
};
typedef DoublyLinkedList<VMBusChannelInfo> VMBusChannelInfoList;


typedef void (VMBus::*VMBusEventFlagsHandler)(int32 cpu);


class VMBus {
public:
									VMBus(device_node* node);
									~VMBus();
			status_t				InitCheck() const { return fStatus; }

			uint32					GetVersion() const { return fVersion; }
			status_t				OpenChannel(uint32 channel, uint32 gpadl, uint32 rxOffset,
										hyperv_bus_callback callback, void* callbackData);
			status_t				CloseChannel(uint32 channel);
			status_t				AllocateGPADL(uint32 channel, uint32 length, void** _buffer,
										uint32* _gpadl);
			status_t				FreeGPADL(uint32 channel, uint32 gpadl);
			status_t				SignalChannel(uint32 channel);

private:
			status_t				_InitHypercalls();
			uint16					_HypercallPostMessage(phys_addr_t physAddr);
			uint16					_HypercallSignalEvent(uint32 connId);

			status_t				_InitInterrupts();
	static	void					_InitInterruptCPUHandler(void* data, int cpu);
			void					_InitInterruptCPU(int32 cpu);
	static	acpi_status				_InterruptACPICallback(ACPI_RESOURCE* res, void* context);
	static	int32					_InterruptHandler(void* data);
			int32					_Interrupt();
			void					_InterruptEventFlags(int32 cpu);
			void					_InterruptEventFlagsLegacy(int32 cpu);
			void					_InterruptEventFlagsNull(int32 cpu);
	static	void					_MessageDPCHandler(void* arg);
			void					_MessageDPC(int32_t cpu);

			VMBusMsgInfo*			_AllocMsgInfo();
			void					_ReturnFreeMsgInfo(VMBusMsgInfo* msgInfo);
	inline	status_t				_WaitForMsgInfo(VMBusMsgInfo* msgInfo);
	inline	void					_AddActiveMsgInfo(VMBusMsgInfo* msgInfo, uint32 respType, uint32 respData = 0);
	inline	void					_RemoveActiveMsgInfo(VMBusMsgInfo* msgInfo);
			void					_NotifyActiveMsgInfo(uint32 respType, uint32 respData,
										vmbus_msg *message, uint32 messageSize);
			status_t				_SendMessage(VMBusMsgInfo* msgInfo, uint32 msgSize = 0);
			void					_EomMessage(int32_t cpu);
	static	void					_WriteEomMsr(void* data, int cpu);

			status_t				_ConnectVersion(uint32 version);
			status_t				_Connect();
			status_t				_RequestChannels();
	static	status_t				_ChannelQueueThreadHandler(void* arg);
			status_t				_ChannelQueueThread();
			status_t				_CreateChannel(VMBusChannelInfo* channelInfo);
			void					_FreeChannel(VMBusChannelInfo* channelInfo);
	inline	uint32					_GetGPADLHandle();

private:
			device_node* 			fNode;
			status_t				fStatus;
			void*					fMessageDPCHandle;
			VMBusEventFlagsHandler	fEventFlagsHandler;

			uint8					fInterruptVector;
			int32					fCPUCount;
			VMBusPerCPUInfo*		fCPUData;
			uint32					fVersion;
			uint32					fConnectionId;

			vmbus_event_flags*		fEventFlagsPage;
			void*					fMonitorPage1;
			void*					fMonitorPage2;

			void*					fHypercallPage;
			phys_addr_t				fHyperCallPhysAddr;
			VMBusMsgInfoList		fFreeMsgList;
			VMBusMsgInfoList		fActiveMsgList;
			mutex					fFreeMsgLock;
			mutex					fActiveMsgLock;

			int32					fCurrentGPADLHandle;

			uint32					fMaxChannelsCount;
			uint32					fHighestChannelID;
			VMBusChannelInfo**		fChannels;
			spinlock				fChannelsSpinlock;

			VMBusChannelInfoList	fChannelOfferList;
			VMBusChannelInfoList	fChannelRescindList;
			mutex					fChannelQueueLock;
			sem_id					fChannelQueueSem;
			thread_id				fChannelQueueThread;
};


#endif // VMBUS_PRIVATE_H
