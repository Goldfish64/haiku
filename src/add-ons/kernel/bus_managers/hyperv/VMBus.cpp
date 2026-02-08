/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VMBusPrivate.h"

#define TRACE_VMBUS
#ifdef TRACE_VMBUS
#	define TRACE(x...) dprintf("\33[35mvmbus:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[35mvmbus:\33[0m " x)
#define ERROR(x...)			dprintf("\33[35mvmbus:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

// Ordered list of newest to oldest VMBus versions
static const uint32
sVMBusVersions[] = {
	VMBUS_VERSION_WS2008R2,
	VMBUS_VERSION_WS2008
};


// VMBus message type to size lookup
static const uint32
sVMBusMessageSizes[VMBUS_MSGTYPE_MAX] = {
	0,											// VMBUS_MSGTYPE_INVALID
	sizeof(vmbus_msg_channel_offer),			// VMBUS_MSGTYPE_CHANNEL_OFFER
	sizeof(vmbus_msg_rescind_channel_offer),	// VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER
	sizeof(vmbus_msg_request_channels),			// VMBUS_MSGTYPE_REQUEST_CHANNELS
	sizeof(vmbus_msg_request_channels_done),	// VMBUS_MSGTYPE_REQUEST_CHANNELS_DONE
	sizeof(vmbus_msg_open_channel),				// VMBUS_MSGTYPE_OPEN_CHANNEL
	0,											// VMBUS_MSGTYPE_OPEN_CHANNEL_RESPONSE
	0,											// VMBUS_MSGTYPE_CLOSE_CHANNEL
	0,											// VMBUS_MSGTYPE_CREATE_GPADL
	0,											// VMBUS_MSGTYPE_CREATE_GPADL_PAGES
	0,											// VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE
	0,											// VMBUS_MSGTYPE_REMOVE_GPADL
	0,											// VMBUS_MSGTYPE_REMOVE_GPADL_RESPONSE
	0,											// VMBUS_MSGTYPE_FREE_CHANNEL
	sizeof(vmbus_msg_connect),					// VMBUS_MSGTYPE_CONNECT
	sizeof(vmbus_msg_connect_resp),				// VMBUS_MSGTYPE_CONNECT_RESPONSE
	0,											// VMBUS_MSGTYPE_DISCONNECT
	0,											// VMBUS_MSGTYPE_DISCONNECT_RESPONSE
	0,											// 18
	0,											// 19
	0,											// 20
	0,											// 21
	0,											// VMBUS_MSGTYPE_MODIFY_CHANNEL
	0,											// 23
	0											// VMBUS_MSGTYPE_MODIFY_CHANNEL_RESPONSE
};


static void
vmbus_eom(void *data, int cpu)
{
	x86_write_msr(IA32_MSR_HV_EOM, 0);
}


VMBus::VMBus(device_node *node)
	:
	fNode(node),
	fInterface(NULL),
	fCookie(NULL),
	fStatus(B_NO_INIT),
	fDPCHandle(NULL),
	fCPUCount(0),
	fCPUData(NULL),
	fVersion(0),
	fConnectionId(0),
	fHypercallPage(NULL),
	fHyperCallPhysAddr(0)
{
	CALLED();

	// Get the parent info
	device_node* parent = gDeviceManager->get_parent_node(node);
	fStatus = gDeviceManager->get_driver(parent,
		(driver_module_info**)&fInterface, &fCookie);
	gDeviceManager->put_node(parent);

	// VMBus management message queue
	fStatus = gDPC->new_dpc_queue(&fDPCHandle, "hyperv vmbus mgmt msg", B_NORMAL_PRIORITY);
	if (fStatus != B_OK) {
		ERROR("DPC setup failed (%s)\n", strerror(fStatus));
		return;
	}

	fStatus = _AllocData();
	if (fStatus != B_OK) {
		ERROR("CPU data allocation failed (%s)\n", strerror(fStatus));
		return;
	}

	fStatus = _InitHypercalls();
	if (fStatus != B_OK)
		return;

	fStatus = _InitInterrupts();
	if (fStatus != B_OK)
		return;

	fStatus = _Connect();
	if (fStatus != B_OK) {
		ERROR("VMBus connection failed (%s)\n", strerror(fStatus));
		return;
	}

	fStatus = _RequestChannels();
	if (fStatus != B_OK) {
		ERROR("Request VMBus channels failed (%s)\n", strerror(fStatus));
		return;
	}
}


VMBus::~VMBus()
{

}


status_t
VMBus::InitCheck()
{
	return fStatus;
}


status_t
VMBus::_AllocData()
{
	// Allocate an executable page for hypercall usage, assuming its page-aligned here
	fHypercallPage = malloc(B_PAGE_SIZE);
	if (fHypercallPage == NULL)
		return B_ERROR;

	fHyperCallPhysAddr = hyperv_mem_vtophys(fHypercallPage);
	if (fHyperCallPhysAddr == HYPERV_VTOPHYS_ERROR)
		return B_ERROR;

	fCPUCount = smp_get_num_cpus();
	fCPUData = new(std::nothrow) VMBusPerCPUInfo[fCPUCount];
	if (fCPUData == NULL)
		return B_NO_MEMORY;

	for (int32 i = 0; i < fCPUCount; i++) {
		fCPUData[i].vmbus = this;
		fCPUData[i].cpu = i;

		fCPUData[i].messages = (volatile hv_message_page*)
			malloc(sizeof (hv_message_page));
		if (fCPUData[i].messages == NULL)
			return B_NO_MEMORY;

		fCPUData[i].event_flags = (volatile hv_event_flags_page*)
			malloc(sizeof (hv_event_flags_page));
		if (fCPUData[i].event_flags == NULL)
			return B_NO_MEMORY;

		memset((void*)fCPUData[i].messages, 0, sizeof (*fCPUData[i].messages));
		memset((void*)fCPUData[i].event_flags, 0, sizeof (*fCPUData[i].event_flags));
	}
	
	// VMBus event flags and monitor pages
	fEventFlagsPage = malloc(B_PAGE_SIZE);
	if (fEventFlagsPage == NULL)
		return B_NO_MEMORY;
	fMonitorPage1 = malloc(B_PAGE_SIZE);
	if (fMonitorPage1 == NULL)
		return B_NO_MEMORY;
	fMonitorPage2 = malloc(B_PAGE_SIZE);
	if (fMonitorPage2 == NULL)
		return B_NO_MEMORY;

	// VMBus message states
	mutex_init(&fFreeMsgLock, "vmbus free msg lock");
	mutex_init(&fActiveMsgLock, "vmbus active msg lock");

	return B_OK;
}


status_t
VMBus::_InitHypercalls()
{
	// Set the guest ID
	x86_write_msr(IA32_MSR_HV_GUEST_OS_ID, IA32_MSR_HV_GUEST_OS_ID_HAIKU);

	// Enable hypercalls
	uint64 msr = x86_read_msr(IA32_MSR_HV_HYPERCALL);
	msr = ((fHyperCallPhysAddr >> HV_PAGE_SHIFT) << IA32_MSR_HV_HYPERCALL_PAGE_SHIFT)
		| (msr & IA32_MSR_HV_HYPERCALL_RSVD_MASK)
		| IA32_MSR_HV_HYPERCALL_ENABLE;
	x86_write_msr(IA32_MSR_HV_HYPERCALL, msr);

	// Check that hypercalls are enabled
	msr = x86_read_msr(IA32_MSR_HV_HYPERCALL);
	if ((msr & IA32_MSR_HV_HYPERCALL_ENABLE) == 0)
		return B_ERROR;

	TRACE("Hypercalls enabled at %p\n", fHypercallPage);
	return B_OK;
}


// TODO: Move this to its own arch file
uint16
VMBus::_HypercallPostMessage(phys_addr_t physAddr)
{
	uint64 status;
	__asm __volatile("call *%3"
		: "=a" (status)
		: "c" (HYPERCALL_POST_MESSAGE), "d" (physAddr), "m" (fHypercallPage));
	return (uint16) (status & 0xFFFF);
}


uint16
VMBus::_HypercallSignalEvent(uint32 connId)
{
	uint64 status;
	__asm __volatile("call *%3"
		: "=a" (status)
		: "c" (HYPERCALL_SIGNAL_EVENT), "d" (connId), "m" (fHypercallPage));
	return (uint16) (status & 0xFFFF);
}


status_t
VMBus::_InitInterrupts()
{
	// TODO: only configuring CPU 0 right now; for all need to execute on the actual CPU
	uint8 vector = fInterface->get_irq(fCookie) + 0x20;

	// Wire up the interrupt handler
	status_t status = fInterface->setup_interrupt(fCookie, _InterruptHandler, this);
	if (status != B_OK)
		return status;

	phys_addr_t messagesPhysAddr = hyperv_mem_vtophys((void*)fCPUData[0].messages);
	phys_addr_t eventFlagsPhysAddr = hyperv_mem_vtophys((void*)fCPUData[0].event_flags);

	TRACE("SIMP %p SIEFP %p vec %u\n", fCPUData[0].messages, fCPUData[0].event_flags, vector);

	TRACE("SIMP 0x%lX SIEFP 0x%lX\n", messagesPhysAddr, eventFlagsPhysAddr);
	
	// Configure SIMP and SIEFP
	uint64 msr = x86_read_msr(IA32_MSR_HV_SIMP);
	msr = ((messagesPhysAddr >> HV_PAGE_SHIFT) << IA32_MSR_HV_SIMP_PAGE_SHIFT)
		| (msr & IA32_MSR_HV_SIMP_RSVD_MASK)
		| IA32_MSR_HV_SIMP_ENABLE;
	x86_write_msr(IA32_MSR_HV_SIMP, msr);
	TRACE("SIMP new msr 0x%lX\n", msr);

	msr = x86_read_msr(IA32_MSR_HV_SIEFP);
	msr = ((eventFlagsPhysAddr >> HV_PAGE_SHIFT) << IA32_MSR_HV_SIEFP_PAGE_SHIFT)
		| (msr & IA32_MSR_HV_SIEFP_RSVD_MASK)
		| IA32_MSR_HV_SIEFP_ENABLE;
	x86_write_msr(IA32_MSR_HV_SIEFP, msr);
	TRACE("SIEFP new msr 0x%lX\n", msr);

	// Configure interrupt vector for incoming VMBus messages
	msr = x86_read_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_MESSAGE);
	msr = vector | (msr & IA32_MSR_HV_SINT_RSVD_MASK);
	x86_write_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_MESSAGE, msr);
	TRACE("SINT%u new msr 0x%lX\n", VMBUS_SINT_MESSAGE, msr);

	// Configure interrupt vector for VMBus timers
	msr = x86_read_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_TIMER);
	msr = vector | (msr & IA32_MSR_HV_SINT_RSVD_MASK);
	x86_write_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_TIMER, msr);
	TRACE("SINT%u new msr 0x%lX\n", VMBUS_SINT_TIMER, msr);

	// Enable interrupts
	msr = x86_read_msr(IA32_MSR_HV_SCONTROL);
	msr = (msr & IA32_MSR_HV_SCONTROL_RSVD_MASK)
		| IA32_MSR_HV_SCONTROL_ENABLE;
	x86_write_msr(IA32_MSR_HV_SCONTROL, msr);
	TRACE("SCONTROL new msr 0x%lX\n", msr);

	return B_OK;
}


/*static*/ int32
VMBus::_InterruptHandler(void *data)
{
	VMBus* vmbus = reinterpret_cast<VMBus*>(data);
	return vmbus->_Interrupt();
}


int32
VMBus::_Interrupt()
{
	int32 cpu = smp_get_current_cpu();
	volatile hv_message_page* message = fCPUData[cpu].messages;

	// Handoff new VMBus management message to DPC
	if (message->interrupts[VMBUS_SINT_MESSAGE].message_type != HYPERV_MSGTYPE_NONE) {
		gDPC->queue_dpc(fDPCHandle, _DPCHandler, &fCPUData[cpu]);
	}

	return B_HANDLED_INTERRUPT;
}


/*static*/ void
VMBus::_DPCHandler(void *arg)
{
	VMBusPerCPUInfo* cpuData = reinterpret_cast<VMBusPerCPUInfo*>(arg);
	cpuData->vmbus->_DPCMessage(cpuData->cpu);
}


void
VMBus::_DPCMessage(int32_t cpu)
{
	volatile hv_message* hvMessage = &fCPUData[cpu].messages->interrupts[VMBUS_SINT_MESSAGE];
	if (hvMessage->message_type != HYPERV_MSGTYPE_CHANNEL
		|| hvMessage->payload_size < sizeof (vmbus_msg_header)) {
		TRACE("Invalid VMBus Hyper-V message type %u length 0x%X\n", hvMessage->message_type,
			hvMessage->payload_size);
		_EomMessage(cpu);
		return;
	}

	vmbus_msg* message = (vmbus_msg*) hvMessage->data;
	TRACE("New VMBus message type %u length 0x%X\n", message->header.type, hvMessage->payload_size);
	if (message->header.type >= VMBUS_MSGTYPE_MAX
		|| hvMessage->payload_size < sVMBusMessageSizes[message->header.type]) {
		TRACE("Invalid VMBus message type or length");
		_EomMessage(cpu);
		return;
	}

	// Handle channel offers/rescinds only
	switch (message->header.type) {
		case VMBUS_MSGTYPE_CHANNEL_OFFER:
			_HandleChannelOffer(&message->channel_offer);
			break;

		case VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER:
			break;

		default:
			_NotifyActiveMsgInfo(message->header.type, message, hvMessage->payload_size);
			break;
	}

	_EomMessage(cpu);
}


VMBusMsgInfo*
VMBus::_AllocMsgInfo()
{
	VMBusMsgInfo* msgInfo;
	MutexLocker msgLocker(fFreeMsgLock);
	if (fFreeMsgList.Head() != NULL) {
		msgInfo = fFreeMsgList.RemoveHead();
		msgLocker.Unlock();

		msgInfo->resp_type = VMBUS_MSGTYPE_INVALID;
		return msgInfo;
	}
	msgLocker.Unlock();

	msgInfo = new(std::nothrow) VMBusMsgInfo;
	if (msgInfo == NULL)
		return NULL;

	msgInfo->post_msg_physaddr = hyperv_mem_vtophys(&msgInfo->post_msg);
	msgInfo->message = (vmbus_msg*) msgInfo->post_msg.data;
	msgInfo->condition_variable.Init(msgInfo, "vmbus msg info");
	return msgInfo;
}


void
VMBus::_ReturnFreeMsgInfo(VMBusMsgInfo *msgInfo)
{
	MutexLocker msgLocker(fFreeMsgLock);
	fFreeMsgList.Add(msgInfo);
	msgLocker.Unlock();
}


inline status_t
VMBus::_WaitForMsgInfo(VMBusMsgInfo *msgInfo)
{
	return msgInfo->condition_variable.Wait(B_CAN_INTERRUPT);
}


inline void
VMBus::_AddActiveMsgInfo(VMBusMsgInfo *msgInfo, uint32 respType)
{
	MutexLocker msgLocker(fActiveMsgLock);
	msgInfo->resp_type = respType;
	fActiveMsgList.Add(msgInfo);
	msgLocker.Unlock();
}


inline void
VMBus::_RemoveActiveMsgInfo(VMBusMsgInfo *msgInfo)
{
	MutexLocker msgLocker(fActiveMsgLock);
	fActiveMsgList.Remove(msgInfo);
	msgLocker.Unlock();
}


void
VMBus::_NotifyActiveMsgInfo(uint32 respType, vmbus_msg *msg, uint32 msgSize)
{
	MutexLocker msgLocker(fActiveMsgLock);
	VMBusMsgInfo* msgInfo = fActiveMsgList.Head();
	while (msgInfo != NULL) {
		if (msgInfo->resp_type == respType) {
			fActiveMsgList.Remove(msgInfo);
			msgLocker.Unlock();

			memcpy(msgInfo->message, msg, msgSize);
			msgInfo->condition_variable.NotifyAll();
			return;
		}
		
		msgInfo = fActiveMsgList.GetNext(msgInfo);
	}
	msgLocker.Unlock();
}


status_t
VMBus::_SendMessage(VMBusMsgInfo *msgInfo, uint32 msgSize)
{
	uint16 hypercallStatus;
	bool complete = false;
	status_t status;

	if (msgSize == 0) {
		if (msgInfo->message->header.type >= VMBUS_MSGTYPE_MAX)
			return B_ERROR;
		msgSize = sVMBusMessageSizes[msgInfo->message->header.type];
	}

	hypercall_post_msg_input *postMsg = &msgInfo->post_msg;
	postMsg->connection_id = VMBUS_CONNID_MESSAGE;
	postMsg->reserved = 0;
	postMsg->message_type = HYPERV_MSGTYPE_CHANNEL;
	postMsg->data_size = msgSize;

	// Multiple hypercalls together may fail due to lack of host resources, just try again
	for (int i = 0; i < HYPERCALL_MAX_RETRY_COUNT; i++) {
		hypercallStatus = _HypercallPostMessage(msgInfo->post_msg_physaddr);
		switch (hypercallStatus) {
			case HYPERCALL_STATUS_SUCCESS:
				status = B_OK;
				complete = true;
				break;

			case HYPERCALL_STATUS_INSUFFICIENT_MEMORY:
			case HYPERCALL_STATUS_INSUFFICIENT_BUFFERS:
				status = B_NO_MEMORY;
				break;
			
			default:
				status = B_IO_ERROR;
				complete = true;
				break;
		}

		if (complete)
			break;

		snooze(20ULL);
	}

	if (status != B_OK)
		TRACE("Hypercall failed 0x%X\n", hypercallStatus);
	return status;
}


void
VMBus::_EomMessage(int32_t cpu)
{
	// Clear current message
	volatile hv_message* message = &fCPUData[cpu].messages->interrupts[VMBUS_SINT_MESSAGE];
	message->message_type = HYPERV_MSGTYPE_NONE;
	memory_full_barrier();

	// Trigger EOM on target CPU if another message is pending
	if (message->message_flags & HV_MESSAGE_FLAGS_PENDING)
		call_single_cpu(cpu, vmbus_eom, NULL);
}


status_t
VMBus::_ConnectVersion(uint32 version)
{
	VMBusMsgInfo* msgInfo = _AllocMsgInfo();
	if (msgInfo == NULL)
		return B_NO_MEMORY;

	vmbus_msg_connect* message = &msgInfo->message->connect;
	message->header.type = VMBUS_MSGTYPE_CONNECT;
	message->header.reserved = 0;

	message->version = version;
	message->target_cpu = 0;
	message->event_flags_physaddr = hyperv_mem_vtophys(fEventFlagsPage);
	message->monitor1_physaddr = hyperv_mem_vtophys(fMonitorPage1);
	message->monitor2_physaddr = hyperv_mem_vtophys(fMonitorPage2);

	TRACE("Connecting to VMBus version %u.%u\n", GET_VMBUS_VERSION_MAJOR(version),
		GET_VMBUS_VERSION_MINOR(version));

	// Attempt connection with specified version
	_AddActiveMsgInfo(msgInfo, VMBUS_MSGTYPE_CONNECT_RESPONSE);
	status_t status = _SendMessage(msgInfo);
	if (status != B_OK)
		return status;

	// Wait for connection response to come back
	status = _WaitForMsgInfo(msgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(msgInfo);
		_ReturnFreeMsgInfo(msgInfo);
		return status;
	}

	status = msgInfo->message->connect_resp.supported != 0 ? B_OK : B_NOT_SUPPORTED;
	if (status == B_OK)
		fConnectionId = msgInfo->message->connect_resp.connection_id;

	_ReturnFreeMsgInfo(msgInfo);
	TRACE("Connection status (%s)\n", strerror(status));
	return status;
}


status_t
VMBus::_Connect()
{
	uint32 versionCount = sizeof(sVMBusVersions) / sizeof(sVMBusVersions[0]);
	status_t status = B_NOT_INITIALIZED;

	for (uint32 i = 0; i < versionCount; i++) {
		status = _ConnectVersion(sVMBusVersions[i]);
		if (status == B_OK) {
			fVersion = sVMBusVersions[i];
			break;
		}
	}

	if (status != B_OK)
		return status;

	TRACE("Connected to VMBus version %u.%u conn id %u\n", GET_VMBUS_VERSION_MAJOR(fVersion),
		GET_VMBUS_VERSION_MINOR(fVersion), fConnectionId);
	return B_OK;
}


status_t
VMBus::_RequestChannels()
{
	VMBusMsgInfo* msgInfo = _AllocMsgInfo();
	if (msgInfo == NULL)
		return B_NO_MEMORY;

	vmbus_msg_request_channels* message = &msgInfo->message->request_channels;
	message->header.type = VMBUS_MSGTYPE_REQUEST_CHANNELS;
	message->header.reserved = 0;

	// Start request channels
	TRACE("Request channels start\n");
	_AddActiveMsgInfo(msgInfo, VMBUS_MSGTYPE_REQUEST_CHANNELS_DONE);
	status_t status = _SendMessage(msgInfo);
	if (status != B_OK)
		return status;

	// Wait for done message
	status = _WaitForMsgInfo(msgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(msgInfo);
		_ReturnFreeMsgInfo(msgInfo);
		return status;
	}
	_ReturnFreeMsgInfo(msgInfo);

	TRACE("Request channels done\n");
	return B_OK;
}


void
VMBus::_HandleChannelOffer(vmbus_msg_channel_offer *message)
{
	char str[100];

	uuid_unparse(message->type_id, str);
	TRACE("New VMBus channel %u, type %s\n", message->channel_id, str);
}
