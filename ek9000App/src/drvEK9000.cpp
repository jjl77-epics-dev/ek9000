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

std::vector<drvEK9000*> devices;

/* Quick conversion macro to convert from bytes to modbus registers */
#define BYTES_TO_REG(x) (((x) % 2) == 0 ? (x) / 2 : ((x) / 2) + 1)

drvEK9000::drvEK9000(const char* ek, const char* port, const char* ipport, const char* ip) :
	drvModbusAsyn(port, ipport, 0, 2, -1, 65535, dataTypeUInt16, 0, ""),
	name(ek)
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
		asynStatus status;
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
		status = _this->doModbusIO(0, MODBUS_READ_INPUT_REGISTERS, INPUT_PDO_START, _this->inputSwapSpace, 0xFF); /* Copy 0xFF bytes (the size of the PDO) */
		status = _this->doModbusIO(0, MODBUS_READ_DISCRETE_INPUTS, INPUT_COIL_START, _this->inputBitSwap, 0xFF);  /* Copy 0xFF bytes (the size of the PDO) */

		/* Trace the flow of the program */
		step = "SwapRegisters";
		asynPrint(_this->pasynUserSelf, ASYN_TRACE_FLOW, "%s: %s\n", function, step);

		/* Compare our output swap spaces */
		epicsSpinLock(_this->swapMutex);

		/* Compare the output PDO and output swap space, and copy it over if they don't match */
		if(memcmp(_this->outputPDO, _this->outputSwapSpace, OUTPUT_REG_SIZE * sizeof(uint16_t)) == 0)
		{
			oreg = true;
			memcpy(_this->outputSwapSpace, _this->outputPDO, OUTPUT_REG_SIZE * sizeof(uint16_t));
		}

		/* Same thing as above; compare and swap output bit PDOs */
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
		status = _this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_REGISTERS, OUTPUT_PDO_START, _this->outputSwapSpace, OUTPUT_REG_SIZE);
		status = _this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_COILS, OUTPUT_COIL_START, _this->outputBitSwap, OUTPUT_BIT_SIZE);

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
			
			/* Get the next request in the queue */
			req = _this->coeRequests.front();
			_this->coeRequests.pop_front();
			
			/* Lock our CoE spinlock */
			epicsSpinUnlock(_this->coeMutex);
			
			/* Perform the IO & compute the total delta t */
			coe_dt += _this->doCoEIO(req);

			/* In the event of us running over the poll period, do not sleep, just rerun the poll! */
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
	ret.v = INT_MAX;

	switch(type)
	{
		case 0: ret.v = epicsAtomicGetIntT((int*)&this->outputBitPDO[addr]); break;
		case 1: ret.v = epicsAtomicGetIntT((int*)&this->inputBitPDO[addr]); break;

		/* For analog output records, we should subtract the start address so we don't overrun the buffer */
		case 2:
			addr -= (0x800-1);
			ret.v = epicsAtomicGetIntT((int*)&this->outputPDO[addr]);
			break;
		
		case 3: ret.v = epicsAtomicGetIntT((int*)&this->inputPDO[addr]); break;
		default: break;
	}
	return ret.i16[0];
}

/**
 * Initialize terminal mappings
 * Coupler must be connected for this to work!
 * This must also be called from the constructor, and nowhere else,
 * it's not thread safe
 */ 
void drvEK9000::MapTerminals()
{
	epicsUInt16 terminalids[0xFF];
	terminal_t terminals[0xFF];

	/* Starting points of PDOs */
	int inp_start = 0x1, outp_start = 0x800, inb_start = 0, outb_start = 0;

	/* Read the registers */
	this->doModbusIO(0, MODBUS_READ_INPUT_REGISTERS, EK9K_SLAVE_MAP_REG, terminalids, 0xFF);

	/* Compute locations in the input/output register space, which will only be analog terms */
	for(int i = 0; terminalids[i] != 0 && i < 0xFF; i++)
	{
		const STerminalInfoConst_t* info = util::FindTerminal(terminalids[i]);
		if(!info) continue;

		/* Ignore everything less than 3k */
		if(info->m_nID < 3000) continue;

		/* Set the terminal info */
		terminals[i].in_start = inp_start;
		terminals[i].out_start = outp_start;
		terminals[i].id = terminalids[i];

		/* Determine the type */
		if(terminalids[i] >= 3000 && terminalids[i] < 4000) terminals[i].type = terminal_t::AI;
		if(terminalids[i] >= 4000 && terminalids[i] < 5000) terminals[i].type = terminal_t::AO;
		if(terminalids[i] >= 1000 && terminalids[i] < 2000) terminals[i].type = terminal_t::BI;
		if(terminalids[i] >= 2000 && terminalids[i] < 3000) terminals[i].type = terminal_t::BO;

		inp_start += info->m_nInputSize;
		outp_start += info->m_nOutputSize;

	}

	/* Do the same for the input bits */
	for(int i = 0; terminalids[i] != 0 && i < 0xFF; i++)
	{
		const STerminalInfoConst_t* info = util::FindTerminal(terminalids[i]);
		if(!info) continue;
		if(info->m_nID >= 3000) continue;

		outb_start += info->m_nOutputSize;
		inb_start += info->m_nInputSize;
	}


}

/**
 * Forcefully performs CoE IO
 * Not thread-safe
 * rw = true means write
 * rw = false means read
 */ 
bool drvEK9000::doCoEIO(bool rw, epicsUInt16 termid, epicsUInt16 index, epicsUInt16 subindex, epicsUInt16 bytesize, epicsUInt16* pbuf)
{
	assert(bytesize <= 250);
	assert(pbuf != NULL);

	/* Write */
	if(rw)
	{
		size_t real_size = 0;
		asynStatus status;
		epicsUInt16 send_data[0xFF] = {
			0x1,				/* execute */
			termid | 0x8000,	/* Termid with bit 0x8000 set for write */
			subindex ^ 0xFF00,	/* subindex, Clear out the high bits here as they're unused */
			bytesize,			/* Size in bytes of transfer */
			0					/* ADS Error code */
		};
		/* Copy into the send_data buffer starting with the 6th element */
		memcpy(&send_data[5], pbuf, bytesize < 250 ? bytesize : 250);

		/* Compute real size of the buffer we're going to send */
		real_size = 5 + (bytesize/2);
		real_size = (bytesize % 2 != 0) ? (real_size + 1) : real_size;

		/* Now actually do the IO */
		status = this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_REGISTERS, REG_0x1400, send_data, real_size);

		return status == asynSuccess;
	}
	/* Read */
	else
	{
		asynStatus status;
		epicsUInt16 status_reg = 0, real_size;
		/* The initial request */
		epicsUInt16 send_data[0xFF] = {
			0x0,				/* just set this to zero for reads */
			termid ^ 0x8000,	/* bit 15 must be 0 for reading */
			index,				/* COE index */
			subindex ^ 0xFF00,	/* Clear high 8 bits */
			bytesize,
			0,					/* ADS error code */
		};

		/* Write out everything */
		status = this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_REGISTERS, REG_0x1400, send_data, 0x5 /* Number of registers */);

		/* Did not work :( */
		if(status != asynSuccess) return false;

		/* Compute real size of the buffer we're going to send */
		real_size = 5 + (bytesize/2);
		real_size = (bytesize % 2 != 0) ? (real_size + 1) : real_size;
	
		/**
		 * Polling loop
		 * The ek9k wants us to read 12 registers here for some reason
		 */ 
		do 
		{
			epicsThreadSleep(0.01);
			status = this->doModbusIO(0, MODBUS_READ_HOLDING_REGISTERS, REG_0x1400, send_data, real_size);
			
			/* safety measure here: break if the read has failed */
			if(status != asynSuccess) break;
			
			status_reg = send_data[0];
		} while(status_reg & 0x200);

		/* Failure to read if 0x10X is set */
		if(send_data[0] & 0x100) return false;

		/* Copy over read data */
		memcpy(pbuf, &send_data[5], bytesize);
		return true;
	}
}

void drvEK9000::DumpInfo()
{
}

/* Enqueues a CoE IO request, optionally putting it at the front of the queue */
void drvEK9000::RequestCoEIO(coe_req_t req, bool immediate)
{
	epicsSpinLock(this->coeMutex);

	/* If immediate, put before everything else */
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

/**
 * Basically just rips a coe_req_t structure apart
 */ 
bool drvEK9000::doCoEIO(coe_req_t req)
{
	return this->doCoEIO(req.type == COE_REQ_WRITE, req.termid, req.index, req.subindex, BYTES_TO_REG(req.length), req.pdata);
}

void ek9000Register(const iocshArgBuf* args)
{
	const char* ek9k = args[0].sval;
	const char* port = args[1].sval;
	const char* ip = args[2].sval;

	/* Check if already registered */
	for(auto x : devices)
	{
		if(strcmp(x->name, ek9k) == 0)
		{
			epicsStdoutPrintf("Device %s already registered.\n", ek9k);
			return;
		}
	}

	/* Create the octet port name */
	char octetport[1024]; 
	snprintf(octetport, 1024, "%s_OCTETPORT", port);

	/* Configure IP port and create EK9K */
	drvAsynIPPortConfigure(octetport, ip, 0, 0, 0);
	auto ek = new drvEK9000(ek9k, port, octetport, ip);

	devices.push_back(ek);
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
