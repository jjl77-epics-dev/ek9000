/*
 * util.cpp
 *
 * Common utilities for use with epics
 *
 */ 
#include "ekUtil.h"
#include "drvEK9000.h"
#include <memory.h>
#include <stdlib.h>
#include <vector>
#include <epicsPrint.h>
#include <epicsStdio.h>
#include <epicsTime.h>
#include <epicsStdio.h>

using namespace util;

/* Maintain a list of handles to iocsh args */
/* This really wont be needed, but it helps us pretend that we aren't leaking memory */
struct iocshHandles_t
{
	iocshArg* args;
	iocshArg** pargs;
	iocshFuncDef func;
};
std::vector<iocshHandles_t*> functions;

/* Register iocsh functions */
void util::iocshRegister(const char* name, void(*pfn)(const iocshArgBuf*), std::initializer_list<iocshArg> args)
{
	iocshHandles_t* handle = static_cast<iocshHandles_t*>(malloc(sizeof(iocshHandles_t)));
	handle->args = static_cast<iocshArg*>(malloc(args.size() * sizeof(iocshArg)));
	handle->pargs = static_cast<iocshArg**>(malloc(args.size() * sizeof(iocshArg*)));
	auto it = args.begin();
	for(int i = 0; i < args.size() && it; i++, it++)
		handle->args[i] = *it;
	for(int i = 0; i < args.size(); i++)
		handle->pargs[i] = &handle->args[i];
	handle->func = {name, (int)args.size(), handle->pargs};
	::iocshRegister(&handle->func, pfn);
	functions.push_back(handle);
}

/*
 * @ek9k_name,slave_num,channel
 */ 
void* util::parseAndCreateDpvt(char* instio)
{
	if(!instio) return nullptr;
	int pindex = 0;
	char* newinstio = strdup(instio);
	
	size_t len = strlen(newinstio);
	size_t num_commas = 0;
	for(int i = 0; i < len; i++) if(newinstio[i] == ',') num_commas++;
	char** pointers = static_cast<char**>(malloc(sizeof(char*) * num_commas));
	char* prevptr = nullptr;
	int ptrpos = 0;
	for(int i = 0; i < len; i++)
	{
		if(!prevptr)
		{
			prevptr = &newinstio[i];
			continue;
		}
		if(newinstio[i] == ',')
		{
			newinstio[i] = 0;
			pointers[ptrpos] = prevptr;
			ptrpos++;
		}
	}

	const char* ek9k_name = pointers[0];
	const char* pos_num = pointers[1];
	const char* channel = pointers[2];

	terminal_dpvt_t dpvt;
	dpvt.pdrv = drvEK9000::FindByName(ek9k_name);

	if(!dpvt.pdrv)
	{
		util::Log("device '%s' not found.", instio);
		return nullptr;
	}
}

const STerminalInfoConst_t* util::FindTerminal(int id)
{
	for(int i = 0; i < NUM_TERMINALS; i++)
		if(g_pTerminalInfos[i]->m_nID == id) return g_pTerminalInfos[i];
	return nullptr;
}

void util::Log(const char* fmt, ...)
{
	time_t clk = time(0);
	tm _tm;
	
	epicsTime_localtime(&clk, &_tm);
	epicsPrintf("%i:%i ", _tm.tm_hour, _tm.tm_min);
	
	va_list list;
	va_start(list, fmt);
	epicsVprintf(fmt, list);
	va_end(list);
}

void util::Warn(const char* fmt, ...)
{
	epicsTimeStamp stmp;
	epicsTimeGetCurrent(&stmp);
	
	char txt[40];
	epicsTimeToStrftime(txt, 40, "%Y/%m/%d %H:%M:%S.%03f ", &stmp);
	epicsPrintf("%s", txt);
	
	va_list list;
	va_start(list, fmt);
	epicsVprintf(fmt, list);
	va_end(list);
}

void util::Error(const char* fmt, ...)
{
	epicsTimeStamp stmp;
	epicsTimeGetCurrent(&stmp);
	
	char txt[40];
	epicsTimeToStrftime(txt, 40, "%Y/%m/%d %H:%M:%S.%03f ", &stmp);
	epicsPrintf("%s", txt);

	va_list list;
	va_start(list, fmt);
	epicsVprintf(fmt, list);
	va_end(list);
}