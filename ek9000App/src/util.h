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

namespace util
{
	void iocshRegister(const char* name, void(*pfn)(const iocshArgBuf*), std::initializer_list<iocshArg> args);

	class epicsSpinLock
	{
	private:
		int iflag;
	public:
		epicsSpinLock();
		~epicsSpinLock();

		/**
		 * \brief Checks if the lock is locked or not
		 */ 
		bool isLocked() const;

		/**
		 * \brief Locks the spinlock
		 */ 
		void lock();

		/**
		 * \brief Unlocks the spinlock
		 */ 
		void unlock();
	};
}
