/*
 * Copyright 2026 John Davis
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_IC_BASE_H_
#define _HYPERV_IC_BASE_H_

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hyperv.h>
#include <hyperv_reg.h>
#include <vmbus_reg.h>

#include "Driver.h"
#include "ICProtocol.h"


class ICBase {
public:
								ICBase(device_node* node);
	virtual						~ICBase();
			status_t			InitCheck() const { return fStatus; }

protected:
	virtual void				_GetMessageVersions(uint32* _versions[],
									uint32* _versionCount) const = 0;
	virtual uint16				_GetMessageType() const = 0;
	virtual uint32				_GetPacketBufferLength() const { return HV_IC_PKTBUFFER_SIZE; }
	virtual	status_t			_Connect(uint32 txLength, uint32 rxLength);
	virtual	void				_Disconnect();
	virtual void				_HandleProtocolNegotiated(uint32 version) = 0;
	virtual void				_HandleMessageReceived(hv_ic_msg* icMessage) = 0;
	virtual void				_HandleMessageSent(hv_ic_msg* icMessage);

protected:
			status_t			fStatus;

private:
			status_t			_NegotiateProtocol(hv_ic_msg_negotiate* message);
	static	void				_CallbackHandler(void* arg);
			void				_Callback();

private:
			device_node*		fNode;
			void*				fPacket;

			hyperv_device_interface*	fHyperV;
			hyperv_device				fHyperVCookie;
};


#endif // _HYPERV_IC_BASE_H_
