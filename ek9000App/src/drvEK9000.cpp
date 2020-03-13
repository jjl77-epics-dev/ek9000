/*
 *
 *	drvEK9000.cpp
 *
 * Implementation of simple EK9K driver code
 *
 */ 
#include <drvModbusAsyn.h>
#include <drvAsynIPPort.h>
#include <asynPortDriver.h>
#include <modbus.h>
#include <modbusInterpose.h>
#include <asynOctetSyncIO.h>
#include <epicsExport.h>
#include <iocsh.h>
#include <epicsString.h> 
#include <asynPortClient.h>
#include <asynInt32.h>
#include <unordered_map>
#include <epicsAtomic.h>
#include <epicsThread.h>
#include <epicsSpin.h>
#include <epicsTime.h>
#include <epicsStdlib.h>
#include <time.h>
#include <queue>

#include "drvEK9000.h"
#include "util.h"

drvEK9000::drvEK9000(const char* port, const char* ipport, const char* ip) :
	drvModbusAsyn(port, ipport, 0, 2, -1, 65535, dataTypeUInt16, 0, "")
{
	this->swapMutex = epicsSpinCreate();
	this->coeMutex = epicsSpinCreate();
}

void drvEK9000::StartPollThread()
{
	/* Create the name for the thread */
	char name[128];
	snprintf(name, 128, "%s_POLLTHREAD", port);
	
	this->pollThread = epicsThreadCreate(name, epicsThreadPriorityHigh, epicsThreadStackBig, drvEK9000::PollThreadFunc, (void*)this);
}

void drvEK9000::PollThreadFunc(void* lparam)
{
	const char* function = "drvEK9000::PollThreadFunc";
	const char* step = "ReadInputRegisters";
	while(1)
	{
		drvEK9000* _this = (drvEK9000*)lparam;
		bool oreg = false, obit = false;
		float dt, coe_dt;

		/* compute the change in time */
		dt = ((clock()) / (CLOCKS_PER_SEC)/1000.0f) - _this->prevTime;

		/* Check the time change to make sure we have not possibly missed a poll period */
		if(dt > _this->pollPeriod * 1000.0f)
		{
			asynPrint(_this->pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s: Total time between poll calls exceeds poll period! dt=%f, period=%f",
					function, step, dt, (_this->pollPeriod*1000.0f));
		}

		asynPrint(_this->pasynUserSelf, ASYN_TRACE_FLOW, "%s: %s\n", function, step);

		/* In order to reduce the number of mutex locks for the swap space mutex, let's read input data now rather than later */
		_this->doModbusIO(0, MODBUS_READ_INPUT_REGISTERS, INPUT_PDO_START, _this->inputSwapSpace, 0xFF);
		_this->doModbusIO(0, MODBUS_READ_DISCRETE_INPUTS, INPUT_COIL_START, _this->inputBitSwap, 0xFF);

		step = "SwapRegisters";
		asynPrint(_this->pasynUserSelf, ASYN_TRACE_FLOW, "%s: %s\n", function, step);

		/* Compare our output swap spaces */
		epicsSpinLock(_this->swapMutex);

		if(memcmp(_this->outputPDO, _this->outputSwapSpace, OUTPUT_REG_SIZE * sizeof(uint16_t)) == 0)
		{
			oreg = true;
			memcpy(_this->outputSwapSpace, _this->outputPDO, OUTPUT_REG_SIZE * sizeof(uint16_t));
		}
		if(memcmp(_this->outputBitPDO, _this->outputBitSwap, OUTPUT_BIT_SIZE * sizeof(uint16_t)) == 0)
		{
			obit = false;
			memcpy(_this->outputBitSwap, _this->outputBitPDO, OUTPUT_BIT_SIZE * sizeof(uint16_t));
		}
		/* For the input PDO's lets copy in the new data */
		memcpy(_this->inputPDO, _this->inputSwapSpace, INPUT_REG_SIZE * sizeof(uint16_t));
		memcpy(_this->inputBitPDO, _this->inputBitSwap, INPUT_BIT_SIZE * sizeof(uint16_t));
		/* Unlock the swap guard */
		epicsSpinUnlock(_this->swapMutex);
		
		step = "WriteOutputRegisters";
		asynPrint(_this->pasynUserSelf, ASYN_TRACE_FLOW, "%s: %s\n", function, step);
		
		/* Finally perform the actual IO */
		_this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_REGISTERS, OUTPUT_PDO_START, _this->outputSwapSpace, OUTPUT_REG_SIZE);
		_this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_COILS, OUTPUT_COIL_START, _this->outputBitSwap, OUTPUT_BIT_SIZE);

		/* Set the last time */
		_this->prevTime = (clock()) / (CLOCKS_PER_SEC/1000.0f);

		step = "DoCoEIO";
		asynPrint(_this->pasynUserSelf, ASYN_TRACE_FLOW, "%s: %s\n", function, step);

		/* Here we will handle all of the CoE IO requests, as many as possible at least */
		/* The internal CoE IO function will return a delta t for how long the request took, 
		 * so we can ensure we don't miss any polling periods */
		coe_dt = 0.0f;
		while(1)
		{
			coe_req_t req;
			int coe_dt;
			
			/* C++ containers are not thread safe :( */
			epicsSpinLock(_this->coeMutex);
			/* Nothing to do ?? */
			if(_this->coeRequests.size() <= 0) break;
			req = _this->coeRequests.front();
			_this->coeRequests.pop_front();
			epicsSpinLock(_this->coeMutex);
			
			/* Perform the IO & compute the total delta t */
			coe_dt += _this->doCoEIO(req);

			/* In the event of us running over the poll period, do not sleep, just poll! */
			if(coe_dt >= (_this->pollPeriod * 1000.0f))
			{
				asynPrint(_this->pasynUserSelf, ASYN_TRACE_WARNING, "%s: %s: Poll period exceeded for missed due to CoE requests or other latency! coe_dt=%d\n", 
						function, step, coe_dt);
				continue;
			}

			/* If within 10ms of the next poll period, it's cutting it kinda close */
			if(coe_dt >= (_this->pollPeriod * 1000.0f - 10.0f))
				break;
		}

		/* Sleep for the poll period minus the dt */
		epicsThreadSleep(_this->pollPeriod - coe_dt);
	}
}

/* Atomically read a register,
 * type 0 = bo, 1 = bi, 2 = ao, 3 = ai */
uint16_t drvEK9000::ReadRegisterAtomic(int addr, int type)
{
	union {
		int v;
		uint16_t i16[2];
	} ret;

	switch(type)
	{
		case 0: 
			ret.v = epicsAtomicGetIntT((int*)&this->outputBitPDO[addr]);
			break;
		case 1:
			ret.v = epicsAtomicGetIntT((int*)&this->inputBitPDO[addr]);
			break;
		case 2:
			addr -= (0x800-1);
			ret.v = epicsAtomicGetIntT((int*)&this->outputPDO[addr]);
			break;
		case 3:
			ret.v = epicsAtomicGetIntT((int*)&this->inputPDO[addr]);
			break;
		default: break;
	}
	return ret.i16[0];
}

void drvEK9000::DumpInfo()
{
}

void drvEK9000::RequestCoEIO(coe_req_t req, bool immediate)
{
	epicsSpinLock(this->coeMutex);
	if(immediate)
		this->coeRequests.push_front(req);
	else
		this->coeRequests.push_back(req);
	epicsSpinUnlock(this->coeMutex);
}

void drvEK9000::RequestCoEIO(coe_req_t* req, int nreq, bool immediate)
{
	epicsSpinLock(this->coeMutex);
	for(int i = 0; i < nreq; i++)
	{
		if(immediate)
			this->coeRequests.push_front(req[i]);
		else
			this->coeRequests.push_back(req[i]);
	}
	epicsSpinUnlock(this->coeMutex);
}

int drvEK9000::doCoEIO(coe_req_t req)
{
	/* Read = 0 */
	if(req.type == 0)
	{

	}
	/* Write = 1 */
	else
	{

	}
}

void ek9000Register(const iocshArgBuf* args)
{	
}

void ek9000AddTerminal(const iocshArgBuf* args)
{

}

void ek9k_register()
{
	util::iocshRegister("ek9000Register", ek9000Register,
			{{"ek9k-name", iocshArgString},
			{"port", iocshArgString},
			{"ip", iocshArgString}});
}

epicsExportRegistrar(ek9k_register);
