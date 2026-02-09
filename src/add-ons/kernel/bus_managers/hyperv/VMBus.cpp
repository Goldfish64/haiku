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

// VMBus device pretty name lookup
static const struct {
	const char* type_id;
	const char* name;
} sVMBusPrettyNames[] = {
	{ VMBUS_TYPE_AVMA,			HYPERV_PRETTYNAME_AVMA },
	{ VMBUS_TYPE_BALLOON,		HYPERV_PRETTYNAME_BALLOON },
	{ VMBUS_TYPE_DISPLAY,		HYPERV_PRETTYNAME_DISPLAY },
	{ VMBUS_TYPE_FIBRECHANNEL,	HYPERV_PRETTYNAME_FIBRECHANNEL },
	{ VMBUS_TYPE_FILECOPY,		HYPERV_PRETTYNAME_FILECOPY },
	{ VMBUS_TYPE_HEARTBEAT,		HYPERV_PRETTYNAME_HEARTBEAT },
	{ VMBUS_TYPE_IDE,			HYPERV_PRETTYNAME_IDE },
	{ VMBUS_TYPE_KEYBOARD,		HYPERV_PRETTYNAME_KEYBOARD },
	{ VMBUS_TYPE_KVP,			HYPERV_PRETTYNAME_KVP },
	{ VMBUS_TYPE_MOUSE,			HYPERV_PRETTYNAME_MOUSE },
	{ VMBUS_TYPE_NETWORK,		HYPERV_PRETTYNAME_NETWORK },
	{ VMBUS_TYPE_PCI,			HYPERV_PRETTYNAME_PCI },
	{ VMBUS_TYPE_RDCONTROL,		HYPERV_PRETTYNAME_RDCONTROL },
	{ VMBUS_TYPE_RDMA,			HYPERV_PRETTYNAME_RDMA },
	{ VMBUS_TYPE_RDVIRT,		HYPERV_PRETTYNAME_RDVIRT },
	{ VMBUS_TYPE_SCSI,			HYPERV_PRETTYNAME_SCSI },
	{ VMBUS_TYPE_SHUTDOWN,		HYPERV_PRETTYNAME_SHUTDOWN },
	{ VMBUS_TYPE_TIMESYNC,		HYPERV_PRETTYNAME_TIMESYNC },
	{ VMBUS_TYPE_VSS,			HYPERV_PRETTYNAME_VSS }
};


VMBus::VMBus(device_node *node)
	:
	fNode(node),
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
	// Get the VMBus ACPI device
	char acpiVMBusName[255];
	status_t status = gACPI->get_device(VMBUS_ACPI_HID_NAME, 0,
		acpiVMBusName, sizeof(acpiVMBusName));
	if (status != B_OK) {
		ERROR("Could not locate VMBus in ACPI\n");
		return status;
	}
	TRACE("VMBus ACPI: %s\n", acpiVMBusName);

	acpi_handle acpiVMBusHandle;
	status = gACPI->get_handle(NULL, acpiVMBusName, &acpiVMBusHandle);
	if (status != B_OK)
		return status;

	uint8 irq;
	status = gACPI->walk_resources(acpiVMBusHandle, (ACPI_STRING)"_CRS",
		_InterruptACPICallback, &irq);
	if (status != B_OK)
		return status;
	if (irq == 0)
		return B_IO_ERROR;

	// Wire up the interrupt handler to the ACPI provided IRQ
	// TODO: Get the vector offset here for x86, and determine vector on ARM64
	uint8 vector = irq + 0x20;
	TRACE("VMBus irq interrupt line: %u, vector: %u\n", irq, vector);
	status = install_io_interrupt_handler(irq, _InterruptHandler, this, 0);
	if (status != B_OK) {
		ERROR("Can't install interrupt handler\n");
		return status;
	}

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


/*static*/ acpi_status
VMBus::_InterruptACPICallback(ACPI_RESOURCE* res, void* context)
{
	uint8* irq = static_cast<uint8*>(context);

	// Grab the first IRQ only. Gen1 usually has two IRQs, Gen2 just one.
	// Only one IRQ is required for the VMBus device.
	if (res->Type == ACPI_RESOURCE_TYPE_IRQ && *irq == 0)
		*irq = res->Data.Irq.Interrupt;
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
		call_single_cpu(cpu, _WriteEomMsr, NULL);
}


/*static*/ void
VMBus::_WriteEomMsr(void *data, int cpu)
{
	x86_write_msr(IA32_MSR_HV_EOM, 0);
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

	status_t status = _SendMessage(msgInfo);
	if (status != B_OK)
		return status;

	_ReturnFreeMsgInfo(msgInfo);
	return B_OK;
}


void
VMBus::_HandleChannelOffer(vmbus_msg_channel_offer *message)
{
	char typeStr[37];
	char instanceStr[37];

	snprintf(typeStr, sizeof (typeStr), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		message->type_id.data1, message->type_id.data2, message->type_id.data3,
		message->type_id.data4[0], message->type_id.data4[1], message->type_id.data4[2],
		message->type_id.data4[3], message->type_id.data4[4], message->type_id.data4[5],
		message->type_id.data4[6], message->type_id.data4[7]);
	snprintf(instanceStr, sizeof (instanceStr), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		message->instance_id.data1, message->instance_id.data2, message->instance_id.data3,
		message->instance_id.data4[0], message->instance_id.data4[1], message->instance_id.data4[2],
		message->instance_id.data4[3], message->instance_id.data4[4], message->instance_id.data4[5],
		message->instance_id.data4[6], message->instance_id.data4[7]);
	TRACE("New VMBus channel %u type %s inst %s\n", message->channel_id, typeStr, instanceStr);

	// Get the pretty name
	const char* prettyName = HYPERV_PRETTYNAME_UNKNOWN;
	uint32 numNames = sizeof (sVMBusPrettyNames) / sizeof (sVMBusPrettyNames[0]);
	for (uint32 i = 0; i < numNames; i++) {
		if (strcmp(typeStr, sVMBusPrettyNames[i].type_id) == 0) {
			prettyName = sVMBusPrettyNames[i].name;
			break;
		}
	}

	device_attr attributes[] = {
		{ B_DEVICE_BUS, B_STRING_TYPE,
			{ .string = HYPERV_BUS_NAME }},
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = prettyName }},
		{ HYPERV_CHANNEL_ID_ITEM, B_UINT32_TYPE,
			{ .ui32 = message->channel_id }},
		{ HYPERV_DEVICE_TYPE_ITEM, B_STRING_TYPE,
			{ .string = typeStr }},
		{ HYPERV_INSTANCE_ID_ITEM, B_STRING_TYPE,
			{ .string = instanceStr }},
		NULL
	};

	// Publish child device node for the VMBus channel
	gDeviceManager->register_node(fNode, HYPERV_DEVICE_MODULE_NAME,
		attributes, NULL, NULL);
}


static status_t
hyperv_detect()
{
	CALLED();

	// Check for hypervisor.
	cpu_ent *cpu = get_cpu_struct();
	if ((cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) == 0) {
		TRACE("No hypervisor detected\n");
		return B_ERROR;
	}

	// Check for Hyper-V CPUID leaves.
	cpuid_info cpuInfo;
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HYPERVISOR, 0);
	if (cpuInfo.regs.eax < IA32_CPUID_LEAF_HV_IMP_LIMITS) {
		TRACE("Not running on Hyper-V\n");
		return B_ERROR;
	}

	// Check for Hyper-V signature.
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HV_INT_ID, 0);
	if (cpuInfo.regs.eax != HV_CPUID_INTERFACE_ID) {
		TRACE("Not running on Hyper-V\n");
		return B_ERROR;
	}

#ifdef TRACE_HYPERV
	get_cpuid(&cpuInfo, IA32_CPUID_LEAF_HV_SYS_ID, 0);
	TRACE("Hyper-V version: %d.%d.%d [SP%d]\n", cpuInfo.regs.ebx >> 16, cpuInfo.regs.ebx & 0xFFFF,
		cpuInfo.regs.eax, cpuInfo.regs.ecx);
#endif

	return B_OK;
}


static float
vmbus_supports_device(device_node* parent)
{
	CALLED();
	const char* bus;

	// Check if the parent is the root node
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK) {
		TRACE("Could not find required attribute device/bus\n");
		return -1;
	}

	if (strcmp(bus, "root") != 0)
		return 0.0f;

	status_t status = hyperv_detect();
	if (status != B_OK)
		return 0.0f;
	
	return 0.8f;
}


static status_t
vmbus_init_driver(device_node* node, void** _driverCookie)
{
	CALLED();

	VMBus* vmbus = new(std::nothrow) VMBus(node);
	if (vmbus == NULL) {
		ERROR("Unable to allocate VMBus\n");
		return B_NO_MEMORY;
	}

	status_t status = vmbus->InitCheck();
	if (status != B_OK) {
		ERROR("failed to set up VMBus device object\n");
		delete vmbus;
		return status;
	}
	TRACE("VMBus object created\n");

	*_driverCookie = vmbus;
	return B_OK;
}


static void
vmbus_uninit_driver(void* driverCookie)
{
	CALLED();
	VMBus* vmbus = (VMBus*)driverCookie;
	delete vmbus;
}


static status_t
vmbus_register_device(device_node* parent)
{
	CALLED();
	device_attr attributes[] = {
		{ B_DEVICE_BUS, B_STRING_TYPE,
			{ .string = HYPERV_BUS_NAME }},
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = HYPERV_PRETTYNAME_VMBUS }},
		{ NULL }
	};

	return gDeviceManager->register_node(parent, HYPERV_VMBUS_MODULE_NAME,
		attributes, NULL, NULL);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


driver_module_info gVMBusModule = {
	{
		HYPERV_VMBUS_MODULE_NAME,
		0,
		std_ops
	},
	vmbus_supports_device,
	vmbus_register_device,
	vmbus_init_driver,
	vmbus_uninit_driver,
	NULL,	// removed device
	NULL,	// register child devices
	NULL,	// rescan bus
};
