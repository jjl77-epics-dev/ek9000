/*
 *
 * drvEK9000.h
 *
 * Driver for the EK9000 EtherCAT to Modbus coupler
 *
 */ 
#pragma once

#include <drvModbusAsyn.h>
#include <epicsAtomic.h>
#include <epicsStdio.h>
#include <epicsStdlib.h>
#include <epicsMutex.h>
#include <epicsTypes.h>

/* Common EK9000 registers */
#define STATUS_REGISTER_BASE 	0x1000 
#define BUS_COUPLER_ID_REG 		0x1000 
#define BUS_COUPLER_ID_REG_END	0x1006
#define PDO_SIZE_AO_REG			0x1010
#define PDO_SIZE_AI_REG			0x1011
#define PDO_SIZE_BO_REG			0x1012
#define PDO_SIZE_BI_REG 		0x1013
#define WDT_CUR_TIME_REG		0x1020
#define NUM_FALLBACKS_REG		0x1021
#define NUM_TCP_CONN_REG		0x1022
#define HARDWARE_VER_REG		0x1030
#define SOFT_VER_MAIN_REG		0x1031
#define SOFT_VER_SUBMAIN_REG	0x1032
#define SOFT_VER_BETA_REG		0x1033
#define SERIAL_NUM_REG			0x1034
#define MFG_DAY_REG				0x1035
#define MFG_MONTH_REG			0x1036
#define MFG_YEAR_REG			0x1037
#define EBUS_STAT_REG			0x1040
#define WDT_TIME_REG			0x1120
#define WDT_RESET_REG			0x1121
#define WDT_TYPE_REG			0x1122
#define FALLBACK_MODE_REG		0x1123
#define WRITELOCK_REG			0x1124
#define EBUS_CTRL_REG			0x1140
#define FIRST_STATUS_REGISTER	BUS_COUPLER_ID_REG 
#define LAST_STATUS_REGISTER 	EBUS_CTRL_REG 

/* The control registers */
#define REG_0x1400				0x1400
#define REG_0x1401				0x1401
#define REG_0x1402 				0x1402
#define REG_0x1403 				0x1403
#define REG_0x1404 				0x1404
#define REG_0x1405 				0x1405
#define REG_DATA_START			0x1406
#define REG_DATA_END 			0x14FF 

typedef struct
{
	int length;
	uint16_t* pdata;
	int adserr;
} coe_resp_t;

typedef struct
{
	int subindex, index, length;

	/*
	 * This pointer will be reused in the response for any returned
	 * data
	 */ 
	uint16_t* pdata;

	/*
	 * Pointer to callback for completion
	 */ 
	void(*pfnCallback)(void*, coe_resp_t);

	/* pvt is private data you can attach to this for the callback */
	void* pvt;
} coe_req_t;

