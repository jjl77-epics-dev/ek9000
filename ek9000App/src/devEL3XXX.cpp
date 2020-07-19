/*
 *
 * devEL3XXX.cpp
 *
 * Analog input terminals
 *
 */ 

#include <aiRecord.h>
#include <epicsExport.h>
#include <epicsMath.h>
#include <epicsStdio.h>
#include <epicsStdlib.h>
#include <epicsAssert.h>
#include <dbAccess.h>
#include <devSup.h>
#include <alarm.h>
#include <epicsString.h>
#include <dbScan.h>
#include <aiRecord.h>
#include <iocsh.h>
#include <callback.h>
#include <alarm.h>
#include <recGbl.h>

#include "ekUtil.h"
#include "drvEK9000.h"

static long el30xx_read_record(void* precord);
static long el30xx_init_record(void* precord);
static long el33xx_read_record(void* precord);
static long el33xx_init_record(void* precord);

#pragma pack(1)
struct el30xx_input_compact_t {
	uint32_t inp;
};

struct el30xx_input_t {
	uint32_t status;
	uint32_t inp;
};

struct el33xx_input_t {

};
#pragma pack()

/*
 * Device support for basic analog input records with 
 * 32-bits per channel in standard mode, 16-bit status, 
 * 16-bit adc count.
 * this also supports compact PDO mode, with 16-bits per channel,
 * no status, only adc counts
 */ 
struct {
	long        number;
	DEVSUPFUN   report;
	DEVSUPFUN   init;
	DEVSUPFUN   init_record;
	DEVSUPFUN   get_ioint_info;
	DEVSUPFUN   read_ai;
	DEVSUPFUN   special_linconv;
} devEL30XX = {
	6,
	NULL,
	NULL,
	el30xx_init_record,
	NULL,
	el30xx_read_record,
	NULL
};

/*
 * Device support for EL33XX thermocouple modules
 * These have a total of 48-bits per channel
 */ 
struct {
    long        number;
    DEVSUPFUN   report;
    DEVSUPFUN   init;
    DEVSUPFUN   init_record;
    DEVSUPFUN   get_ioint_info;
    DEVSUPFUN   read_ai;
    DEVSUPFUN   special_linconv;
} devEL33XX = {
    6,
    NULL,
    NULL,
    el33xx_init_record,
    NULL,
    el33xx_read_record,
    NULL
};

/*
=======================================
   
   EL30XX Support

=======================================
*/

static long el30xx_init_record(void* precord)
{
	aiRecord* prec = static_cast<aiRecord*>(precord);
	prec->dpvt = util::parseAndCreateDpvt(prec->inp.value.instio.string);
	return prec->dpvt ? 0 : 1;
}

static long el30xx_read_record(void* precord)
{
	aiRecord* prec = static_cast<aiRecord*>(precord);
	terminal_dpvt_t* dpvt = static_cast<terminal_dpvt_t*>(prec->dpvt);
	if(!dpvt->pdrv->doRead(*dpvt, dpvt->rwbuf, dpvt->len))
	{
		recGblSetSevr(precord, READ_ALARM, MAJOR_ALARM);
	}
	else
	{
		if(dpvt->flags & EL30XX_FLAG_STANDARD)
		{
			el30xx_input_t* pdo = static_cast<el30xx_input_t*>(dpvt->rwbuf);
			prec->rval = pdo->inp;
		}
		else
		{
			el30xx_input_compact_t* pdo = static_cast<el30xx_input_compact_t*>(dpvt->rwbuf);
			prec->rval = pdo->inp;
		}
	}
	prec->udf = FALSE;
	prec->pact = FALSE;
}

/*
=======================================
   
   EL33XX Support

=======================================
*/

static long el33xx_init_record(void* precord)
{
	aiRecord* prec = static_cast<aiRecord*>(precord);
	prec->dpvt = util::parseAndCreateDpvt(prec->inp.value.instio.string);
	return prec->dpvt ? 0 : 1;
}

static long el33xx_read_record(void* precord)
{
	aiRecord* prec = static_cast<aiRecord*>(precord);
	terminal_dpvt_t* dpvt = static_cast<terminal_dpvt_t*>(prec->dpvt);
	if(!dpvt->pdrv->doRead(*dpvt, dpvt->rwbuf, dpvt->len))
	{
		recGblSetSevr(precord, READ_ALARM, MAJOR_ALARM);
	}
	else
	{
		el33xx_input_t* pdo = static_cast<el33xx_input_t*>(dpvt->rwbuf);
		
	}
	prec->udf = FALSE;
	prec->pact = FALSE;
}


