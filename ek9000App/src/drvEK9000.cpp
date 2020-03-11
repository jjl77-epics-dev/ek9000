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

#include "epicsThread.h"
#include "util.h"

std::unordered_map<const char*, asynOctetClient> ek9k;

#define INPUT_PDO_START 0x1
#define OUTPUT_PDO_START 0x800
#define INPUT_COIL_START 0x0
#define OUTPUT_COIL_START 0x0

class drvEK9000 : public drvModbusAsyn
{

};

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
