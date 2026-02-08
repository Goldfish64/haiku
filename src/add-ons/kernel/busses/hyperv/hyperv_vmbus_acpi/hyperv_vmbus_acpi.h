/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef HYPERV_VMBUS_ACPI_H
#define HYPERV_VMBUS_ACPI_H

#include <KernelExport.h>

typedef struct {
	uint8	irq;
	uint8	irq_triggering;
	uint8	irq_polarity;
	uint8	irq_shareable;
} vmbus_acpi_crs;

typedef struct {
	vmbus_acpi_crs crs;
} vmbus_acpi_info;

#endif
