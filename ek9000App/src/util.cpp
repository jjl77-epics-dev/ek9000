/*
 * util.cpp
 *
 * Common utilities for use with epics
 *
 */ 
#include "util.h"
#include <memory.h>
#include <stdlib.h>
#include <vector>
#include <epicsPrint.h>
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
	
	for(char* subst = strtok(instio, ","); subst; subst = strtok(NULL, ","), pindex++)
	{

	}

	if(pindex < 2)
	{
		epicsStdoutPrintf("Syntax error in instio string: %s\n", instio);
		return nullptr;
	}
}
