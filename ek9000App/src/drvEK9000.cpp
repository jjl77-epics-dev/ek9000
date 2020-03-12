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

#include "drvEK9000.h"
#include "util.h"

#define INPUT_PDO_START 	0x1
#define OUTPUT_PDO_START 	0x800
#define INPUT_COIL_START 	0x0
#define OUTPUT_COIL_START 	0x0

#define INPUT_REG_SIZE		0xFF
#define OUTPUT_REG_SIZE		0xFF
#define INPUT_BIT_SIZE		0xFF
#define OUTPUT_BIT_SIZE		0xFF 

/*
 *	drvEK9000
 *
 * This is a simple driver for the EK9000 bus coupler
 * This class itself is just tasked with reading all of the registers from the device
 * quickly. Device drivers then atomically read register values from the exposed 
 * PDOs. Mutex locking is minimal here.
 *
 */ 
class drvEK9000 : public drvModbusAsyn
{
private:


public:
	drvEK9000(const char* port, const char* ipport, const char* ip);
	~drvEK9000();

	void PopulateSlaveList();
	void StartPollThread();
	
	/*
	 * Enqueues a CoE IO request with the driver
	 * this can be requested to be "immediate" or not
	 * The pfnCallback 
	 * Requests are served in the order in which they're enqueued; 
	 */ 
	void RequestCoEIO(coe_req_t req, bool immediate=false);
	void RequestCoEIO(coe_req_t* req, int nreq, bool immediate=false);

	static void PollThreadFunc(void* lparam);

	const char* port, *ipport, *ip;

	/* Input/Output pdo sizes */
	int outputBytes, inputBytes, inputBits, outputBits;

	/* Register spaces */
	uint16_t inputPDO[INPUT_REG_SIZE];
	uint16_t outputPDO[OUTPUT_REG_SIZE];
	bool inputBitPDO[INPUT_BIT_SIZE];
	bool outputBitPDO[OUTPUT_BIT_SIZE];

	/* Register swap spaces */
	/* These spaces will be sent over the wire, and should not be touched 
	 * by any device code. They are temporary holding buffers */
	uint16_t inputSwapSpace[INPUT_REG_SIZE];
	uint16_t outputSwapSpace[OUTPUT_REG_SIZE];
	uint16_t inputBitSwap[INPUT_BIT_SIZE];
	uint16_t outputBitSwap[OUTPUT_BIT_SIZE];

	/* Mutex for swapping of input/output spaces */
	/* Ideally a spinlock is used here since we're only going to be locking for a couple us */
	epicsSpinId swapMutex;
	 
	/* Polling thread handle */
	epicsThreadId pollThread;

	/* Poll period, we sleep for this many seconds */
	double pollPeriod;
};

drvEK9000::drvEK9000(const char* port, const char* ipport, const char* ip) :
	drvModbusAsyn(port, ipport, 0, 2, -1, 65535, dataTypeUInt16, 0, "")
{
	this->swapMutex = epicsSpinCreate();
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
	while(1)
	{
		drvEK9000* _this = (drvEK9000*)lparam;
		bool oreg = false, obit = false;
	
		/* In order to reduce the number of mutex locks for the swap space mutex, let's read input data now rather than later */
		_this->doModbusIO(0, MODBUS_READ_INPUT_REGISTERS, INPUT_PDO_START, _this->inputSwapSpace, 0xFF);
		_this->doModbusIO(0, MODBUS_READ_DISCRETE_INPUTS, INPUT_COIL_START, _this->inputBitSwap, 0xFF);

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
		
		/* Finally perform the actual IO */
		_this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_REGISTERS, OUTPUT_PDO_START, _this->outputSwapSpace, OUTPUT_REG_SIZE);
		_this->doModbusIO(0, MODBUS_WRITE_MULTIPLE_COILS, OUTPUT_COIL_START, _this->outputBitSwap, OUTPUT_BIT_SIZE);
	
		/* Sleep for the poll period */
		epicsThreadSleep(_this->pollPeriod);
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
