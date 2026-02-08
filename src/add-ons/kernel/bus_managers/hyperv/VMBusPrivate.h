/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef VMBUS_PRIVATE_H
#define VMBUS_PRIVATE_H

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_cpu.h>
#include <condition_variable.h>
#include <cpu.h>
#include <dpc.h>
#include <lock.h>
#include <smp.h>
#include <uuid.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>

#include <hyperv_reg.h>
#include <vmbus.h>
#include <vmbus_reg.h>

#include "hyperv_private.h"

class VMBus;

typedef struct {
	VMBus*							vmbus;
	int32_t							cpu;
	volatile hv_message_page*		messages;
	volatile hv_event_flags_page*	event_flags;
} VMBusPerCPUInfo;

 // VMBus message info used for transactions
struct VMBusMsgInfo : DoublyLinkedListLinkImpl<VMBusMsgInfo> {
	hypercall_post_msg_input	post_msg;
	phys_addr_t					post_msg_physaddr;
	vmbus_msg*					message;

	uint32						resp_type;
	ConditionVariable			condition_variable;
};
typedef DoublyLinkedList<VMBusMsgInfo> VMBusMsgInfoList;

class VMBus {
public:
									VMBus(device_node *node);
									~VMBus();
			status_t				InitCheck();

private:
			status_t				_AllocData();
			status_t				_InitHypercalls();
			uint16					_HypercallPostMessage(phys_addr_t physAddr);
			uint16					_HypercallSignalEvent(uint32 connId);

			status_t				_InitInterrupts();
	static	int32					_InterruptHandler(void *data);
			int32					_Interrupt();
	static	void					_DPCHandler(void *data);
			void						_DPCMessage(int32_t cpu);

			VMBusMsgInfo*			_AllocMsgInfo();
			void					_ReturnFreeMsgInfo(VMBusMsgInfo *msgInfo);
	inline	status_t				_WaitForMsgInfo(VMBusMsgInfo *msgInfo);
	inline	void					_AddActiveMsgInfo(VMBusMsgInfo *msgInfo, uint32 respType);
	inline	void					_RemoveActiveMsgInfo(VMBusMsgInfo *msgInfo);
			void					_NotifyActiveMsgInfo(uint32 respType,
										vmbus_msg *message, uint32 messageSize);
			status_t				_SendMessage(VMBusMsgInfo *msgInfo, uint32 msgSize = 0);
			void					_EomMessage(int32_t cpu);

			status_t				_ConnectVersion(uint32 version);
			status_t				_Connect();
			status_t				_RequestChannels();
			void					_HandleChannelOffer(vmbus_msg_channel_offer *message);

private:
			device_node* 			fNode;
			vmbus_bus_interface* 	fInterface;
			void* 					fCookie;
			status_t				fStatus;
			void*					fDPCHandle;

			int32					fCPUCount;
			VMBusPerCPUInfo*		fCPUData;
			void*					fEventFlagsPage;
			void*					fMonitorPage1;
			void*					fMonitorPage2;
			uint32					fVersion;
			uint32					fConnectionId;

			void*					fHypercallPage;
			phys_addr_t				fHyperCallPhysAddr;
			VMBusMsgInfoList		fFreeMsgList;
			VMBusMsgInfoList		fActiveMsgList;
			mutex					fFreeMsgLock;
			mutex					fActiveMsgLock;
};

#endif
