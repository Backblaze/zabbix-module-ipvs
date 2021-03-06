/*
** Zabbix
** Copyright (C) 2001-2014 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** Author: Cao Qingshan <caoqingshan@kingsoft.com>
**
** Version: 1.6 Last update: 2015-08-09 15:00
**
**/

#include "common.h"
#include "sysinc.h"
#include "module.h"
#include "zbxjson.h"
#include "log.h"

/* the variable keeps timeout setting for item processing */
static int	item_timeout = 0;

int	zbx_module_ipvs_stats(AGENT_REQUEST *request, AGENT_RESULT *result);
int zbx_module_ipvs_vip_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int zbx_module_ipvs_vip_conns(AGENT_REQUEST *request, AGENT_RESULT *result);

static	ZBX_METRIC keys[] =
/*	KEY                     FLAG			FUNCTION			TEST PARAMETERS */
{
	{"ipvs.stats",		CF_HAVEPARAMS,		zbx_module_ipvs_stats,		NULL},
	{"ipvs.vipdiscovery",	0,	zbx_module_ipvs_vip_discovery,	NULL},
	{"ipvs.vipconns",	CF_HAVEPARAMS,		zbx_module_ipvs_vip_conns,	NULL},
	{NULL}
};

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_api_version                                           *
 *                                                                            *
 * Purpose: returns version number of the module interface                    *
 *                                                                            *
 * Return value: ZBX_MODULE_API_VERSION_ONE - the only version supported by   *
 *               Zabbix currently                                             *
 *                                                                            *
 ******************************************************************************/
int	zbx_module_api_version()
{
	return ZBX_MODULE_API_VERSION_ONE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_timeout                                          *
 *                                                                            *
 * Purpose: set timeout value for processing of items                         *
 *                                                                            *
 * Parameters: timeout - timeout in seconds, 0 - no timeout set               *
 *                                                                            *
 ******************************************************************************/
void	zbx_module_item_timeout(int timeout)
{
	item_timeout = timeout;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_list                                             *
 *                                                                            *
 * Purpose: returns list of item keys supported by the module                 *
 *                                                                            *
 * Return value: list of item keys                                            *
 *                                                                            *
 ******************************************************************************/
ZBX_METRIC	*zbx_module_item_list()
{
	return keys;
}

int zbx_module_ipvs_vip_conns(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char line[MAX_STRING_LEN];
	FILE	*f;
	int vip_matches = 0;
	char *vipstring;
	unsigned int total_conns = 0;
	unsigned int requested_ip_int;
	unsigned int requested_port_int;
	unsigned int quad1, quad2, quad3, quad4;

	if (request->nparam != 1)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
		return ret;
	}

	vipstring = get_rparam(request, 0);
	sscanf(vipstring, "%u.%u.%u.%u:%u", &quad1,&quad2,&quad3,&quad4,&requested_port_int);
	requested_ip_int = (quad1 << 24) + (quad2 << 16) + (quad3 << 8) + quad4;

	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		while (NULL != fgets(line, sizeof(line), f))
		{
			//TODO: actually find our VIP and parse out connections
			if (0 == strncmp(line, "IP Virtual Server", 18) || 0 == strncmp(line, "Prot LocalAddress:Port", 23) || 0 == strncmp(line, "  -> RemoteAddress:Port", 24))
			{
				zabbix_log(LOG_LEVEL_WARNING,"Skipping line: %s", line);
				continue;
			}
			if (0 == strncmp(line, "TCP", 3) || 0 == strncmp(line, "UDP", 3))
			{
				zabbix_log(LOG_LEVEL_WARNING,"Found VIP line: %s", line);
				unsigned int line_ip_int;
				unsigned int line_port_int;
				sscanf(line, "%*s %x:%x %*s", &line_ip_int, &line_port_int);
				if (requested_ip_int == line_ip_int && requested_port_int == line_port_int)
				{
					zabbix_log(LOG_LEVEL_WARNING,"VIP line matches");
					vip_matches = 1;
					continue;
				}
				else
				{
					zabbix_log(LOG_LEVEL_WARNING,"VIP line does not match");
					vip_matches = 0;
					continue;
				}
			}
			if (1 == vip_matches && 0 == strncmp(line, "  -> ", 5))
			{
				zabbix_log(LOG_LEVEL_WARNING,"Found server line for matching VIP");
				int activeconns = 0;
				sscanf(line,"%*s %*s %*s %*s %u", &activeconns);
				total_conns += activeconns;
				ret = SYSINFO_RET_OK;
			}
			if (0 != strncmp(line, "  -> ", 5))
			{
				// VIP line of unknown type
				vip_matches = 0;
			}
		}
		SET_UI64_RESULT(result, (zbx_uint64_t)total_conns);
		zbx_fclose(f);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Reading /proc/net/ip_vs failed"));
	}
	return ret;

}
int	zbx_module_ipvs_vip_discovery(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char line[MAX_STRING_LEN];
	FILE	*f;


	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		char *buffer = (char *)malloc(sizeof(char)*10);
		zbx_strlcpy(buffer, "{\"data\":[", 10);
		int vipcount=0;
		while (NULL != fgets(line, sizeof(line), f))
		{
			if (0 == strncmp(line, "TCP", 3) || 0 == strncmp(line, "UDP", 3))
			{
				// VIP definition line, add it to the list
				unsigned int ip_int;
				unsigned int port_int = 0;
				char dottedquad_ip_port[22];
				char tempbuff [sizeof(dottedquad_ip_port) + sizeof(char)*15];
				sscanf(line, "%*s %x:%x %*s", &ip_int, &port_int);
				zbx_snprintf(dottedquad_ip_port,sizeof(dottedquad_ip_port), "%d.%d.%d.%d:%d", ip_int >> 24, (ip_int >> 16) & 0xFF, (ip_int >> 8) & 0xFF, ip_int & 0xFF, port_int);
				if (vipcount==0) {
					zbx_snprintf(tempbuff, sizeof(tempbuff), "{\"{#VIP}\":\"%s\"}", dottedquad_ip_port);
				}
				else
				{
					zbx_snprintf(tempbuff, sizeof(tempbuff), ",{\"{#VIP}\":\"%s\"}", dottedquad_ip_port);
				}
				buffer = (char*)realloc(buffer, sizeof(char)*(strlen(buffer) + strlen(tempbuff) + 1));
				strcat(buffer, tempbuff);
				vipcount++;
			}
			else
			{
				// only care about VIPs for discovery, ignore all other lines
				continue;
			}
		}
		buffer = (char*)realloc(buffer, sizeof(char)*(strlen(buffer) + 3));
		strcat(buffer, "]}");
		SET_STR_RESULT(result, strdup(buffer));
		free(buffer);
		zbx_fclose(f);
		ret = SYSINFO_RET_OK;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Reading /proc/net/ip_vs failed"));
	}

	return ret;
}
int	zbx_module_ipvs_stats(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	*key;
	char	line[MAX_STRING_LEN];
	FILE	*f;

	int cps, inpps, outpps, inbps, outbps; 

	if (request->nparam != 1)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
		return ret;
	}

	key = get_rparam(request, 0);

	if (NULL != (f = fopen("/proc/net/ip_vs_stats", "r")))
	{
		while (NULL != fgets(line, sizeof(line), f))
		{   
			if (0 != strncmp(line, " Conns/s", 8))
				continue;

			if(NULL != fgets(line, sizeof(line), f))
			{  
				zbx_rtrim(line, "\n");
				/* for dev
 				zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_stats], line:[%s]", line);
				*/

				if(5 == sscanf(line, "%8X %8X %8X %16X %16X", &cps, &inpps, &outpps, &inbps, &outbps))
				{   
					/* for dev
 					zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_stats], cps:[%d] inpps:[%d] outpps:[%d] inbps:[%d] outbps:[%d]", cps, inpps, outpps, inbps, outbps);
					*/
				}

			}
			if (0 == strcmp(key, "cps")) 
			{
				SET_UI64_RESULT(result, (zbx_uint64_t)cps);
				ret = SYSINFO_RET_OK;
			}
			else if (0 == strcmp(key, "inpps")) 
			{
				SET_UI64_RESULT(result, (zbx_uint64_t)inpps);
				ret = SYSINFO_RET_OK;
			}
			else if (0 == strcmp(key, "outpps")) 
			{
				SET_UI64_RESULT(result, (zbx_uint64_t)outpps);
				ret = SYSINFO_RET_OK;
			}
			else if (0 == strcmp(key, "inbps")) 
			{
				SET_UI64_RESULT(result, (zbx_uint64_t)inbps);
				ret = SYSINFO_RET_OK;
			}
			else if (0 == strcmp(key, "outbps")) 
			{
				SET_UI64_RESULT(result, (zbx_uint64_t)outbps);
				ret = SYSINFO_RET_OK;
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "module [ipvs], func [zbx_module_ipvs_stats], can't find key [%s]", key);
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported key [%s]", key));
			}
		}

		zbx_fclose(f);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse proc file failed"));
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_init                                                  *
 *                                                                            *
 * Purpose: the function is called on agent startup                           *
 *          It should be used to call any initialization routines             *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - module initialization failed               *
 *                                                                            *
 * Comment: the module won't be loaded in case of ZBX_MODULE_FAIL             *
 *                                                                            *
 ******************************************************************************/
int	zbx_module_init()
{
	return ZBX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_uninit                                                *
 *                                                                            *
 * Purpose: the function is called on agent shutdown                          *
 *          It should be used to cleanup used resources if there are any      *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - function failed                            *
 *                                                                            *
 ******************************************************************************/
int	zbx_module_uninit()
{
	return ZBX_MODULE_OK;
}
