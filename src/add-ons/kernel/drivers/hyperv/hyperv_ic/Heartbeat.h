/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_HEARTBEAT_H_
#define _HYPERV_HEARTBEAT_H_

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hyperv.h>
#include <hyperv_reg.h>
#include <vmbus_reg.h>

#include "Driver.h"
#include "ICBase.h"
#include "HeartbeatProtocol.h"

//#define TRACE_HYPERV_HEARTBEAT
#ifdef TRACE_HYPERV_HEARTBEAT
#	define TRACE(x...) dprintf("\33[94mhyperv_heartbeat:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[94mhyperv_heartbeat:\33[0m " x)
#define ERROR(x...)			dprintf("\33[94mhyperv_heartbeat:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#define HYPERV_HEARTBEAT_DRIVER_MODULE_NAME	"drivers/hyperv/hyperv_ic/heartbeat/driver_v1"


class Heartbeat : public ICBase {
public:
								Heartbeat(device_node* node);
								~Heartbeat();

protected:
			void				_GetMessageVersions(uint32* _versions[],
									uint32* _versionCount) const;
			uint16				_GetMessageType() const { return HV_IC_MSGTYPE_HEARTBEAT; }
			void				_HandleProtocolNegotiated(uint32 version);
			void				_HandleMessageReceived(hv_ic_msg* icMessage);
};


#endif // _HYPERV_HEARTBEAT_H_
