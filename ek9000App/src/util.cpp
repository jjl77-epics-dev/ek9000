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

epicsSpinLock::epicsSpinLock() :
	iflag(0)
{

}

epicsSpinLock::~epicsSpinLock()
{

}

bool epicsSpinLock::isLocked() const
{
	return (epicsAtomicGetIntT(&this->iflag) != 0);
}

void epicsSpinLock::lock()
{
	while(epicsAtomicCmpAndSwapIntT(&this->iflag, 0, 1) != 0) {};
}

void epicsSpinLock::unlock()
{
	epicsAtomicSetIntT(&this->iflag, 0);
}
