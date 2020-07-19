/**
 * 
 * ekCoEConfig.cpp - CoE configuration by means of EPICS 
 * 
 */ 
#include "ekCoEConfig.h"

#include <epicsStdio.h>

template<class T>
void coeSetParameter(coe_param_t param, int terminal, T value)
{

}

void coeSetStringParameter(coe_param_t param, int terminal, const char* string)
{

}

template<class T>
void coeReadParameter(coe_param_t param, int terminal, T& outval)
{

}

void coeReadString(coe_param_t param, int terminal, char* outbuf, int len)
{
	
}