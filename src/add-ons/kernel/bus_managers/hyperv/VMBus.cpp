/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VMBusPrivate.h"


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
	sizeof(vmbus_msg_open_channel_resp),		// VMBUS_MSGTYPE_OPEN_CHANNEL_RESPONSE
	sizeof(vmbus_msg_close_channel),			// VMBUS_MSGTYPE_CLOSE_CHANNEL
	0,											// VMBUS_MSGTYPE_CREATE_GPADL
	0,											// VMBUS_MSGTYPE_CREATE_GPADL_PAGES
	sizeof(vmbus_msg_create_gpadl_resp),		// VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE
	sizeof(vmbus_msg_free_gpadl),				// VMBUS_MSGTYPE_FREE_GPADL
	sizeof(vmbus_msg_free_gpadl_resp),			// VMBUS_MSGTYPE_FREE_GPADL_RESPONSE
	sizeof(vmbus_msg_free_channel),				// VMBUS_MSGTYPE_FREE_CHANNEL
	sizeof(vmbus_msg_connect),					// VMBUS_MSGTYPE_CONNECT
	sizeof(vmbus_msg_connect_resp),				// VMBUS_MSGTYPE_CONNECT_RESPONSE
	sizeof(vmbus_msg_disconnect),				// VMBUS_MSGTYPE_DISCONNECT
	0,											// 17
	0,											// 18
	0,											// 19
	0,											// 20
	0,											// 21
	0,											// VMBUS_MSGTYPE_MODIFY_CHANNEL
	0,											// 23
	0											// VMBUS_MSGTYPE_MODIFY_CHANNEL_RESPONSE
};


VMBus::VMBus(device_node *node)
	:
	fNode(node),
	fStatus(B_NO_INIT),
	fMessageDPCHandle(NULL),
	fEventFlagsHandler(&VMBus::_InterruptEventFlagsNull),
	fCPUCount(0),
	fCPUData(NULL),
	fVersion(0),
	fConnectionId(0),
	fHypercallPage(NULL),
	fHyperCallPhysAddr(0),
	fCurrentGPADLHandle(VMBUS_GPADL_NULL),
	fMaxChannelsCount(0),
	fHighestChannelID(0),
	fChannels(NULL),
	fChannelQueueSem(0),
	fChannelQueueThread(0)
{
	CALLED();

	// Allocate an executable page for hypercall usage, assuming its page-aligned here
	fHypercallPage = malloc(HV_PAGE_SIZE);
	if (fHypercallPage == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	physical_entry entry;
	fStatus = get_memory_map(fHypercallPage, 1, &entry, 1);
	if (fStatus != B_OK) {
		return;
	}
	fHyperCallPhysAddr = entry.address;

	// Allocate per-CPU state
	fCPUCount = smp_get_num_cpus();
	fCPUData = new(std::nothrow) VMBusPerCPUInfo[fCPUCount];
	if (fCPUData == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	for (int32 i = 0; i < fCPUCount; i++) {
		fCPUData[i].vmbus = this;
		fCPUData[i].cpu = i;

		fCPUData[i].messages = (volatile hv_message_page*)
			malloc(sizeof(hv_message_page));
		fCPUData[i].event_flags = new(std::nothrow) hv_event_flags_page;
		if (fCPUData[i].messages == NULL || fCPUData[i].event_flags == NULL) {
			fStatus = B_NO_MEMORY;
			return;
		}

		memset((void*)fCPUData[i].messages, 0, sizeof (*fCPUData[i].messages));
		memset((void*)fCPUData[i].event_flags, 0, sizeof (*fCPUData[i].event_flags));
	}

	// Allocate VMBus event flags and monitor pages
	fEventFlagsPage = new(std::nothrow) vmbus_event_flags;
	fMonitorPage1 = malloc(HV_PAGE_SIZE);
	fMonitorPage2 = malloc(HV_PAGE_SIZE);
	if (fEventFlagsPage == NULL || fMonitorPage1 == NULL || fMonitorPage2 == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	mutex_init(&fFreeMsgLock, "vmbus freemsg lock");
	mutex_init(&fActiveMsgLock, "vmbus activemsg lock");
	B_INITIALIZE_SPINLOCK(&fChannelsSpinlock);
	mutex_init(&fChannelQueueLock, "vmbus chnqueue lock");

	// Create VMBus management message queue
	fStatus = gDPC->new_dpc_queue(&fMessageDPCHandle, "hyperv vmbus mgmt msg",
		B_NORMAL_PRIORITY);
	if (fStatus != B_OK)
		return;

	// Create and start channel management thread
	fChannelQueueSem = create_sem(0, "vmbus channel sem");
	if (fChannelQueueSem < B_OK) {
		fStatus = fChannelQueueSem;
		return;
	}

	fChannelQueueThread = spawn_kernel_thread(_ChannelQueueThreadHandler,
		"vmbus channelqueue", B_NORMAL_PRIORITY, this);
	if (fChannelQueueThread < B_OK) {
		fStatus = fChannelQueueThread;
		return;
	}
	resume_thread(fChannelQueueThread);

	// Initialize and enable hypercalls
	fStatus = _InitHypercalls();
	if (fStatus != B_OK)
		return;

	fStatus = _InitInterrupts();
	if (fStatus != B_OK)
		return;

	// Connect to the VMBus
	fStatus = _Connect();
	if (fStatus != B_OK) {
		ERROR("VMBus connection failed (%s)\n", strerror(fStatus));
		return;
	}

	// Get the list of current channels
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
VMBus::OpenChannel(uint32 channel, uint32 gpadl, uint32 rxPageOffset,
	hyperv_bus_callback callback, void* callbackData)
{
	// Channel must be valid
	if (channel >= fMaxChannelsCount)
		return B_BAD_VALUE;

	cpu_status state = disable_interrupts();
	acquire_spinlock(&fChannelsSpinlock);
	VMBusChannelInfo* channelInfo = fChannels[channel];
	release_spinlock(&fChannelsSpinlock);
	restore_interrupts(state);

	if (channelInfo == NULL)
		return B_NAME_NOT_FOUND;

	status_t status = mutex_lock(&channelInfo->lock);
	if (status != B_OK)
		return status;

	// Store the callback
	channelInfo->callback = callback;
	channelInfo->callback_data = callbackData;

	// Create the open channel message
	VMBusMsgInfo* msgInfo = _AllocMsgInfo();
	if (msgInfo == NULL) {
		mutex_unlock(&channelInfo->lock);
		return B_NO_MEMORY;
	}

	vmbus_msg_open_channel* message = &msgInfo->message->open_channel;
	message->header.type = VMBUS_MSGTYPE_OPEN_CHANNEL;
	message->header.reserved = 0;

	message->channel_id = channel;
	message->open_id = channel;
	message->gpadl_id = gpadl;
	message->target_cpu = 0;
	message->rx_page_offset = rxPageOffset;
	memset(message->user_data, 0, sizeof(message->user_data));

	TRACE("Opening channel %u with ring GPADL %u\n", channel, gpadl);

	// Send open channel message to Hyper-V
	_AddActiveMsgInfo(msgInfo, VMBUS_MSGTYPE_OPEN_CHANNEL_RESPONSE, channel);
	status = _SendMessage(msgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(msgInfo);
		_ReturnFreeMsgInfo(msgInfo);
		mutex_unlock(&channelInfo->lock);
		return status;
	}

	// Wait for open channel response to come back
	status = _WaitForMsgInfo(msgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(msgInfo);
		_ReturnFreeMsgInfo(msgInfo);
		mutex_unlock(&channelInfo->lock);
		return status;
	}

	status = (msgInfo->message->open_channel_resp.result == 0
		&& msgInfo->message->open_channel_resp.open_id == channel)
		? B_OK : B_IO_ERROR;
	_ReturnFreeMsgInfo(msgInfo);

	TRACE("Open channel %u status (%s)\n", channel, strerror(status));

	mutex_unlock(&channelInfo->lock);
	return status;
}


status_t
VMBus::CloseChannel(uint32 channel)
{
	return B_ERROR;
}


status_t
VMBus::AllocateGPADL(uint32 channel, uint32 length, void** _buffer, uint32* _gpadl)
{
	// Length must be page-aligned and within bounds
	if (length == 0 || length != HV_PAGE_ALIGN(length))
		return B_BAD_VALUE;

	uint32 pageTotalCount = HV_BYTES_TO_PAGES(length);
	if ((pageTotalCount + 1) > VMBUS_GPADL_MAX_PAGES)
		return B_BAD_VALUE;

	// Channel must be valid
	if (channel >= fMaxChannelsCount)
		return B_BAD_VALUE;

	cpu_status state = disable_interrupts();
	acquire_spinlock(&fChannelsSpinlock);
	VMBusChannelInfo* channelInfo = fChannels[channel];
	release_spinlock(&fChannelsSpinlock);
	restore_interrupts(state);

	if (channelInfo == NULL)
		return B_NAME_NOT_FOUND;

	status_t status = mutex_lock(&channelInfo->lock);
	if (status != B_OK)
		return status;

	// Allocate contigous buffer to back the GPADL
	void* buffer;
	area_id areaid = create_area("gpadl buffer", &buffer, B_ANY_KERNEL_ADDRESS,
		length, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (areaid < B_OK) {
		mutex_unlock(&channelInfo->lock);
		return B_NO_MEMORY;
	}

	// Get physical address of the newly allocated buffer
	physical_entry entry;
	status = get_memory_map(buffer, length, &entry, 1);
	if (status != B_OK) {
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return B_ERROR;
	}
	memset(buffer, 0, length);

	uint32 gpadl = _GetGPADLHandle();

	// Check if additional messages to transfer all page numbers are required
	bool needsAddtMsgs = pageTotalCount > VMBUS_MSG_CREATE_GPADL_MAX_PAGES;
	TRACE("Creating GPADL %u for channel %u with %u pages (multiple: %s)\n", gpadl, channel,
		pageTotalCount, needsAddtMsgs ? "yes" : "no");

	// Allocate GPADL creation message, this will be held onto until the response comes back
	VMBusMsgInfo* createMsgInfo = _AllocMsgInfo();
	if (createMsgInfo == NULL) {
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return B_NO_MEMORY;
	}

	// Populate the GPADL creation message
	uint32 pageMessageCount = needsAddtMsgs
		? VMBUS_MSG_CREATE_GPADL_MAX_PAGES : pageTotalCount;
	uint32 messageLength = sizeof(vmbus_msg_create_gpadl)
		+ (sizeof(uint64) * pageMessageCount);

	vmbus_msg_create_gpadl* createMessage = &createMsgInfo->message->create_gpadl;
	createMessage->header.type = VMBUS_MSGTYPE_CREATE_GPADL;
	createMessage->header.reserved = 0;

	createMessage->channel_id = channel;
	createMessage->gpadl_id = gpadl;
	createMessage->total_range_length = sizeof(createMessage->ranges)
		+ (pageTotalCount * sizeof(uint64));
	createMessage->range_count = 1;
	createMessage->ranges[0].offset = 0;
	createMessage->ranges[0].length = length;

	uint64 currentPageNum = (uint64)(entry.address >> HV_PAGE_SHIFT);
	for (uint32 i = 0; i < pageMessageCount; i++) {
		createMessage->ranges[0].page_nums[i] = currentPageNum++;
	}

	// Send GPADL creation message to Hyper-V
	_AddActiveMsgInfo(createMsgInfo, VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE, gpadl);
	status = _SendMessage(createMsgInfo, messageLength);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(createMsgInfo);
		_ReturnFreeMsgInfo(createMsgInfo);
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return status;
	}

	// Create the additional messages if required
	if (needsAddtMsgs) {
		VMBusMsgInfo* addtMsgInfo = _AllocMsgInfo();
		if (addtMsgInfo == NULL) {
			_RemoveActiveMsgInfo(createMsgInfo);
			_ReturnFreeMsgInfo(createMsgInfo);
			delete_area(areaid);
			mutex_unlock(&channelInfo->lock);
			return B_NO_MEMORY;
		}

		uint32 pagesRemaining = pageTotalCount - pageMessageCount;
		while (pagesRemaining > 0) {
			if (pagesRemaining > VMBUS_MSG_CREATE_GPADL_ADDT_MAX_PAGES)
				pageMessageCount = VMBUS_MSG_CREATE_GPADL_ADDT_MAX_PAGES;
			else
				pageMessageCount = pagesRemaining;

			// Populate the GPADL additional pages message
			messageLength = sizeof(vmbus_msg_create_gpadl)
				+ (sizeof(uint64) * pageMessageCount);

			vmbus_msg_create_gpadl_addt* addtMessage = &addtMsgInfo->message->create_gpadl_addt;
			addtMessage->header.type = VMBUS_MSGTYPE_CREATE_GPADL_ADDT;
			addtMessage->header.reserved = 0;

			addtMessage->gpadl_id = gpadl;
			for (uint32 i = 0; i < pageMessageCount; i++) {
				addtMessage->page_nums[i] = currentPageNum++;
			}

			// Send the GPADL additional pages message to Hyper-V
			status = _SendMessage(addtMsgInfo, messageLength);
			if (status != B_OK) {
				_ReturnFreeMsgInfo(addtMsgInfo);
				_RemoveActiveMsgInfo(createMsgInfo);
				_ReturnFreeMsgInfo(createMsgInfo);
				delete_area(areaid);
				mutex_unlock(&channelInfo->lock);
				return status;
			}

			pagesRemaining -= pageMessageCount;
		}

		_ReturnFreeMsgInfo(addtMsgInfo);
	}

	// Wait for GPADL creation response to come back
	status = _WaitForMsgInfo(createMsgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(createMsgInfo);
		_ReturnFreeMsgInfo(createMsgInfo);
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return status;
	}

	status = createMsgInfo->message->create_gpadl_resp.result == 0 ? B_OK : B_IO_ERROR;
	_ReturnFreeMsgInfo(createMsgInfo);
	if (status != B_OK) {
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return status;
	}

	TRACE("Created GPADL %u for channel %u\n", gpadl, channel);

	// Store the GPADL buffer info for later freeing
	VMBusGPADLInfo* gpadlInfo = new(std::nothrow) VMBusGPADLInfo;
	if (gpadlInfo == NULL) {
		delete_area(areaid);
		mutex_unlock(&channelInfo->lock);
		return B_NO_MEMORY;
	}

	gpadlInfo->gpadl_id = gpadl;
	gpadlInfo->length = length;
	gpadlInfo->areaid = areaid;
	channelInfo->gpadls.Add(gpadlInfo);

	*_buffer = buffer;
	*_gpadl = gpadl;

	mutex_unlock(&channelInfo->lock);
	return B_OK;
}


status_t
VMBus::FreeGPADL(uint32 channel, uint32 gpadl)
{
	return B_ERROR;
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

	// Check event flags first
	(this->*fEventFlagsHandler)(cpu);

	// Handoff new VMBus management message to DPC
	volatile hv_message_page* message = fCPUData[cpu].messages;
	if (message->interrupts[VMBUS_SINT_MESSAGE].message_type != HYPERV_MSGTYPE_NONE) {
		gDPC->queue_dpc(fMessageDPCHandle, _MessageDPCHandler, &fCPUData[cpu]);
	}

	return B_HANDLED_INTERRUPT;
}


void
VMBus::_InterruptEventFlags(int32 cpu)
{
	// Check the SynIC event flags directly
	acquire_spinlock(&fChannelsSpinlock);

	release_spinlock(&fChannelsSpinlock);
}


void
VMBus::_InterruptEventFlagsLegacy(int32 cpu)
{
	// Check the SynIC event flags first, then the VMBus RX event flags
	hv_event_flags_page* eventFlags = fCPUData[cpu].event_flags;
	if (atomic_get_and_set(eventFlags->interrupts[VMBUS_SINT_MESSAGE].iflags32, 0) == 0)
		return;

	acquire_spinlock(&fChannelsSpinlock);

	int32* chanRXFlags = fEventFlagsPage->rx_event_flags.iflags32;
	uint32 flags = static_cast<uint32>(atomic_get_and_set(chanRXFlags, 0));
	for (uint32 i = 1; i <= fHighestChannelID; i++) {
		if ((i % 32) == 0)
			flags = static_cast<uint32>(atomic_get_and_set(chanRXFlags++, 0));
			
		if (flags & 0x1)
			fChannels[i]->callback(fChannels[i]->callback_data);
		flags >>= 1;
	}

	release_spinlock(&fChannelsSpinlock);
}


void
VMBus::_InterruptEventFlagsNull(int32 cpu)
{
}


/*static*/ void
VMBus::_MessageDPCHandler(void *arg)
{
	VMBusPerCPUInfo* cpuData = reinterpret_cast<VMBusPerCPUInfo*>(arg);
	cpuData->vmbus->_MessageDPC(cpuData->cpu);
}


void
VMBus::_MessageDPC(int32_t cpu)
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

	if (message->header.type == VMBUS_MSGTYPE_CHANNEL_OFFER) {
		vmbus_msg_channel_offer* offerMessage = &message->channel_offer;

		if (offerMessage->channel_id < fMaxChannelsCount) {
			VMBusChannelInfo* channelInfo = new(std::nothrow) VMBusChannelInfo;
			if (channelInfo != NULL) {
				channelInfo->channel_id = offerMessage->channel_id;
				channelInfo->type_id = offerMessage->type_id;
				channelInfo->instance_id = offerMessage->instance_id;

				channelInfo->vmbus = this;
				mutex_init(&channelInfo->lock, "vmbus chn lock");
				new (&channelInfo->gpadls) VMBusGPADLInfoList;
				channelInfo->node = NULL;

				// Add new channel to list and signal the channel handler thread
				mutex_lock(&fChannelQueueLock);
				fChannelOfferList.Add(channelInfo);
				mutex_unlock(&fChannelQueueLock);

				release_sem_etc(fChannelQueueSem, 1, B_DO_NOT_RESCHEDULE);
			}
		} else {
			TRACE("Invalid VMBus channel ID %u received!\n", offerMessage->channel_id);
		}
	} else if (message->header.type == VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER) {
		// TODO: Implement channel removals
	} else {
		uint32 respData = 0;
		switch (message->header.type) {
			case VMBUS_MSGTYPE_OPEN_CHANNEL_RESPONSE:
				respData = message->open_channel_resp.channel_id;
				break;

			case VMBUS_MSGTYPE_CREATE_GPADL_RESPONSE:
				respData = message->create_gpadl_resp.gpadl_id;
				break;

			case VMBUS_MSGTYPE_FREE_GPADL_RESPONSE:
				respData = message->free_gpadl_resp.gpadl_id;
				break;

			default:
				break;
		}
		_NotifyActiveMsgInfo(message->header.type, respData, message, hvMessage->payload_size);
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
VMBus::_AddActiveMsgInfo(VMBusMsgInfo *msgInfo, uint32 respType, uint32 respData)
{
	MutexLocker msgLocker(fActiveMsgLock);
	msgInfo->resp_type = respType;
	msgInfo->resp_data = respData;
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
VMBus::_NotifyActiveMsgInfo(uint32 respType, uint32 respData, vmbus_msg *msg, uint32 msgSize)
{
	MutexLocker msgLocker(fActiveMsgLock);
	VMBusMsgInfo* msgInfo = fActiveMsgList.Head();
	while (msgInfo != NULL) {
		if (msgInfo->resp_type == respType && msgInfo->resp_data == respData) {
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
			return B_BAD_VALUE;
		msgSize = sVMBusMessageSizes[msgInfo->message->header.type];
		if (msgSize == 0)
			return B_BAD_VALUE;
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
	message->event_flags_physaddr = hyperv_mem_vtophys((void*)fEventFlagsPage);
	message->monitor1_physaddr = hyperv_mem_vtophys(fMonitorPage1);
	message->monitor2_physaddr = hyperv_mem_vtophys(fMonitorPage2);

	TRACE("Connecting to VMBus version %u.%u\n", GET_VMBUS_VERSION_MAJOR(version),
		GET_VMBUS_VERSION_MINOR(version));

	// Attempt connection with specified version
	_AddActiveMsgInfo(msgInfo, VMBUS_MSGTYPE_CONNECT_RESPONSE);
	status_t status = _SendMessage(msgInfo);
	if (status != B_OK) {
		_RemoveActiveMsgInfo(msgInfo);
		_ReturnFreeMsgInfo(msgInfo);
		return status;
	}

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

	if (fVersion == VMBUS_VERSION_WS2008 || fVersion == VMBUS_VERSION_WS2008R2) {
		fMaxChannelsCount = VMBUS_MAX_CHANNELS_LEGACY;
		fEventFlagsHandler = &VMBus::_InterruptEventFlagsLegacy;
	} else {
		fMaxChannelsCount = VMBUS_MAX_CHANNELS;
		fEventFlagsHandler = &VMBus::_InterruptEventFlags;
	}

	// Allocate array for channel data
	fChannels = new(std::nothrow) VMBusChannelInfo*[fMaxChannelsCount];
	if (fChannels == NULL)
		return B_NO_MEMORY;

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


/*static*/ status_t
VMBus::_ChannelQueueThreadHandler(void *arg)
{
	VMBus* vmbus = reinterpret_cast<VMBus*>(arg);
	return vmbus->_ChannelQueueThread();
}


status_t
VMBus::_ChannelQueueThread()
{
	while (acquire_sem(fChannelQueueSem) == B_OK) {

		// Fetch the next added and/or removed channels
		mutex_lock(&fChannelQueueLock);
		VMBusChannelInfo* newChannel = fChannelOfferList.RemoveHead();
		VMBusChannelInfo* oldChannel = fChannelRescindList.RemoveHead();
		mutex_unlock(&fChannelQueueLock);

		// Add and/or remove the channels from the active list
		cpu_status state = disable_interrupts();
		acquire_spinlock(&fChannelsSpinlock);

		if (newChannel != NULL) {
			fChannels[newChannel->channel_id] = newChannel;
			if (fHighestChannelID < newChannel->channel_id)
				fHighestChannelID = newChannel->channel_id;
		}
		if (oldChannel != NULL)
			fChannels[oldChannel->channel_id] = NULL;

		release_spinlock(&fChannelsSpinlock);
		restore_interrupts(state);

		// Handle new channel registration
		if (newChannel != NULL) {
			char typeStr[37];
			char instanceStr[37];

			snprintf(typeStr, sizeof (typeStr), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				newChannel->type_id.data1, newChannel->type_id.data2, newChannel->type_id.data3,
				newChannel->type_id.data4[0], newChannel->type_id.data4[1], newChannel->type_id.data4[2],
				newChannel->type_id.data4[3], newChannel->type_id.data4[4], newChannel->type_id.data4[5],
				newChannel->type_id.data4[6], newChannel->type_id.data4[7]);
			snprintf(instanceStr, sizeof (instanceStr), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				newChannel->instance_id.data1, newChannel->instance_id.data2, newChannel->instance_id.data3,
				newChannel->instance_id.data4[0], newChannel->instance_id.data4[1], newChannel->instance_id.data4[2],
				newChannel->instance_id.data4[3], newChannel->instance_id.data4[4], newChannel->instance_id.data4[5],
				newChannel->instance_id.data4[6], newChannel->instance_id.data4[7]);
			TRACE("Registering VMBus channel %u type %s inst %s\n", newChannel->channel_id, typeStr, instanceStr);

			// Get the pretty name
			char prettyName[sizeof (HYPERV_PRETTYNAME_VMBUS_DEVICE_FMT) + 8];
			snprintf(prettyName, sizeof (prettyName), HYPERV_PRETTYNAME_VMBUS_DEVICE_FMT,
				newChannel->channel_id);

			device_attr attributes[] = {
				{ B_DEVICE_BUS, B_STRING_TYPE,
					{ .string = HYPERV_BUS_NAME }},
				{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
					{ .string = prettyName }},
				{ HYPERV_CHANNEL_ID_ITEM, B_UINT32_TYPE,
					{ .ui32 = newChannel->channel_id }},
				{ HYPERV_DEVICE_TYPE_ITEM, B_STRING_TYPE,
					{ .string = typeStr }},
				{ HYPERV_INSTANCE_ID_ITEM, B_STRING_TYPE,
					{ .string = instanceStr }},
				{ NULL }
			};

			// Publish child device node for the VMBus channel
			gDeviceManager->register_node(fNode, HYPERV_DEVICE_MODULE_NAME,
				attributes, NULL, &newChannel->node);
		}

		// Handle old channel deregistration
		if (oldChannel != NULL) {
			// Acquire and destroy the lock
			mutex_lock(&oldChannel->lock);
			mutex_destroy(&oldChannel->lock);

			// TODO
		}
	}

	return B_OK;
}


inline uint32
VMBus::_GetGPADLHandle()
{
	uint32 gpadl;
	do {
		gpadl = static_cast<uint32>(atomic_add(&fCurrentGPADLHandle, 1));
	} while (gpadl == VMBUS_GPADL_NULL);
	return gpadl;
}
