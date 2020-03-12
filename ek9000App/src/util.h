/*
 * util.h
 *
 * Common utilities for use with EPICS
 *
 */ 
#pragma once

#include <initializer_list>
#include <iocsh.h>

#include <epicsAtomic.h>

typedef struct 
{
	class drvEK9000* pdrv;
	int slave, terminal, channel;
	int baseaddr, len;
} terminal_dpvt_t;

namespace util
{
	void iocshRegister(const char* name, void(*pfn)(const iocshArgBuf*), std::initializer_list<iocshArg> args);
	
	/*
	 * Parses and creates a device private structure for the terminal
	 * instio is your instio string passed into the record's INP field.
	 */ 
	void* parseAndCreateDpvt(char* instio);
}

