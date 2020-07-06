TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS BELOW HERE

# Build an IOC support library
LIBRARY_IOC += ek9000Support

DBD += devEK9000.dbd
DBD += ek9000.dbd
DBD += ek9000Include.dbd

USR_CXXFLAGS += --std=c++17 -g

# Used for code improvement purposes
ifneq ($(ALL_WARNINGS),)
USR_CXXFLAGS += -Wunused -Wall -Wextra -Wpedantic
endif 

# Records/Device support sources
ek9000Support_SRCS += diag.cpp
ek9000Support_SRCS += drvEK9000.cpp
ek9000Support_SRCS += util.cpp

ek9000Support_LIBS += $(EPICS_BASE_IOC_LIBS)
ek9000Support_LIBS += modbus
ek9000Support_LIBS += asyn
ek9000Support_LIBS += motor

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD EXTRA GNUMAKE RULES BELOW HERE

