/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VMBusPrivate.h"


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


uint16
VMBus::_HypercallPostMessage(phys_addr_t physAddr)
{
	uint64 status;

#if defined(__i386__)
	__asm __volatile("call *%5"
		: "=A" (status)
		: "d" (0), "a" (HYPERCALL_POST_MESSAGE), "b" (0), "c" (static_cast<uint32>(physAddr)), "m" (fHypercallPage));
#elif defined(__x86_64__)
	__asm __volatile("call *%3"
		: "=a" (status)
		: "c" (HYPERCALL_POST_MESSAGE), "d" (physAddr), "m" (fHypercallPage));
#endif

	return (uint16) (status & 0xFFFF);
}


uint16
VMBus::_HypercallSignalEvent(uint32 connId)
{
	uint64 status;

#if defined(__i386__)
	__asm __volatile("call *%5"
		: "=A" (status)
		: "d" (0), "a" (HYPERCALL_SIGNAL_EVENT), "b" (0), "c" (connId), "m" (fHypercallPage));
#elif defined(__x86_64__)
	__asm __volatile("call *%3"
		: "=a" (status)
		: "c" (HYPERCALL_SIGNAL_EVENT), "d" (connId), "m" (fHypercallPage));
#endif

	return (uint16) (status & 0xFFFF);
}


void
VMBus::_InitInterruptCPU(int32 cpu)
{
	physical_entry messagesEntry;
	status_t status = get_memory_map((void*)fCPUData[cpu].messages, 1, &messagesEntry, 1);
	if (status != B_OK) {
		panic("VMBus failed to get phys for cpu%u messages\n", cpu);
	}

	physical_entry eventFlagsEntry;
	status = get_memory_map((void*)fCPUData[cpu].event_flags, 1, &eventFlagsEntry, 1);
	if (status != B_OK) {
		panic("VMBus failed to get phys for cpu%u event flags\n", cpu);
	}

	phys_addr_t messagesPhysAddr = messagesEntry.address;
	phys_addr_t eventFlagsPhysAddr = eventFlagsEntry.address;

	// Configure SIMP and SIEFP
	uint64 msr = x86_read_msr(IA32_MSR_HV_SIMP);
	msr = ((messagesPhysAddr >> HV_PAGE_SHIFT) << IA32_MSR_HV_SIMP_PAGE_SHIFT)
		| (msr & IA32_MSR_HV_SIMP_RSVD_MASK)
		| IA32_MSR_HV_SIMP_ENABLE;
	x86_write_msr(IA32_MSR_HV_SIMP, msr);
	TRACE("cpu%u: simp new msr 0x%LX\n", cpu, msr);

	msr = x86_read_msr(IA32_MSR_HV_SIEFP);
	msr = ((eventFlagsPhysAddr >> HV_PAGE_SHIFT) << IA32_MSR_HV_SIEFP_PAGE_SHIFT)
		| (msr & IA32_MSR_HV_SIEFP_RSVD_MASK)
		| IA32_MSR_HV_SIEFP_ENABLE;
	x86_write_msr(IA32_MSR_HV_SIEFP, msr);
	TRACE("cpu%u: siefp new msr 0x%LX\n", cpu, msr);

	// Configure interrupt vector for incoming VMBus messages
	msr = x86_read_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_MESSAGE);
	msr = fInterruptVector | (msr & IA32_MSR_HV_SINT_RSVD_MASK);
	x86_write_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_MESSAGE, msr);
	TRACE("cpu%u: sint%u new msr 0x%LX\n", VMBUS_SINT_MESSAGE, cpu, msr);

	// Configure interrupt vector for VMBus timers
	msr = x86_read_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_TIMER);
	msr = fInterruptVector | (msr & IA32_MSR_HV_SINT_RSVD_MASK);
	x86_write_msr(IA32_MSR_HV_SINT0 + VMBUS_SINT_TIMER, msr);
	TRACE("cpu%u: sint%u new msr 0x%LX\n", VMBUS_SINT_TIMER, cpu, msr);

	// Enable interrupts
	msr = x86_read_msr(IA32_MSR_HV_SCONTROL);
	msr = (msr & IA32_MSR_HV_SCONTROL_RSVD_MASK)
		| IA32_MSR_HV_SCONTROL_ENABLE;
	x86_write_msr(IA32_MSR_HV_SCONTROL, msr);
	TRACE("cpu%u: scontrol new msr 0x%LX\n", cpu, msr);
}
