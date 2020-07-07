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

/* List of all terminals */
#include "terminals.h"

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

	/**
	 * Look up a terminal by ID and return a structure containing info about it
	 */ 
	const STerminalInfoConst_t* FindTerminal(int id);

	/**
	 * Logging routines
	 */ 
	void Log(const char* fmt, ...);
	void Warn(const char* fmt, ...);
	void Error(const char* fmt, ...);
}

/**
 * Utility locked semaphore class
 */ 
class lockedSemaphore
{
private:
	int users;
public:
	lockedSemaphore() {
		users = 0;
	};

	~lockedSemaphore() {
	}

	/**
	 * Decrements the number of users
	 */ 
	void removeUser()
	{
		epicsAtomicDecrIntT(&users);
	}

	/**
	 * Increments the number of users
	 * blocks until the spinlock is released
	 */ 
	void addUser()
	{
		while(epicsAtomicGetIntT(&users) == -1) {};
		do {
			int val = epicsAtomicGetIntT(&users);
			if( val == epicsAtomicCmpAndSwapIntT(&users, val, val++))
				break;
		} while(1);
	}

	/**
	 * Same as addUser but it fails if the spinlock is not released
	 */ 
	bool tryAddUser()
	{
		if(epicsAtomicGetIntT(&users) == -1) return false;
		do {
			int val = epicsAtomicGetIntT(&users);
			if( val == epicsAtomicCmpAndSwapIntT(&users, val, val++))
				break;
		} while(1);
		return true;
	}

	/**
	 * Performs a lock operation
	 * blocks until the lock is taken
	 */ 
	void lock()
	{
		while(epicsAtomicCmpAndSwapIntT(&users, 0, -1) != 0) {};
	}

	bool tryLock()
	{
		return epicsAtomicCmpAndSwapIntT(&users, 0, -1) == 0;
	}

	void unlock()
	{
		epicsAtomicSetIntT(&users, 0);
	}


};

