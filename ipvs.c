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

int	zbx_module_ipvs_read_proc_file(void);
int	zbx_module_ipvs_free_memory();
int	zbx_module_ipvs_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int	zbx_module_ipvs_ping(AGENT_REQUEST *request, AGENT_RESULT *result);
int	zbx_module_ipvs_version(AGENT_REQUEST *request, AGENT_RESULT *result);
int	zbx_module_ipvs_hash_table_size(AGENT_REQUEST *request, AGENT_RESULT *result);
int	zbx_module_ipvs_activeconns(AGENT_REQUEST *request, AGENT_RESULT *result);
int	zbx_module_ipvs_inactconns(AGENT_REQUEST *request, AGENT_RESULT *result);

static	ZBX_METRIC keys[] =
/*      KEY                     FLAG			FUNCTION			TEST PARAMETERS */
{
	{"ipvs.discovery",	0,			zbx_module_ipvs_discovery,	NULL},
	{"ipvs.ping",		0,			zbx_module_ipvs_ping,		NULL},
	{"ipvs.version",	0,			zbx_module_ipvs_version,	NULL},
	{"ipvs.hts",		0,			zbx_module_ipvs_hash_table_size,NULL},
	{"ipvs.activeconns",	CF_HAVEPARAMS,		zbx_module_ipvs_activeconns,	NULL},
	{"ipvs.inactconns",	CF_HAVEPARAMS,		zbx_module_ipvs_inactconns,	NULL},
	{NULL}
};

typedef struct ipvs_rs_s
{
	char			addr[16];	/* xxx.xxx.xxx.xxx */      
	char			port[6];	/* 0-65535 */
	char			fwd[10];	/* Masq|Local|Tunnel|Route */
	int			weight;
	zbx_uint64_t		activeconns; 
	zbx_uint64_t		inactconns; 
	struct ipvs_rs_s	*next; 
}
ipvs_rs_t;

typedef struct ipvs_srv_s
{
	char			protocol[4];     /* TCP | UDP */
	char			addr[16];        /* xxx.xxx.xxx.xxx */
	char			port[6];         /* 0-65535 */
	char			sched_name[7];   /* rr|wrr|lc|wlc|lblc|lblcr|dh|sh|sed|nq */
	struct ipvs_srv_s	*next;
	ipvs_rs_t		*ipvs_rs_list;
}
ipvs_srv_t;

ipvs_srv_t	*ipvs_srv_list = NULL;

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


int	zbx_module_ipvs_read_proc_file()
{
    
	int	ret = SYSINFO_RET_FAIL;
	char	line[MAX_STRING_LEN];
	int	tmp1, tmp2, tmp3;
	FILE	*f;
    
	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		while (NULL != fgets(line, sizeof(line), f))
		{
			if (strncmp(line, "TCP", 3) == 0 || strncmp(line, "UDP", 3) == 0)
			{

				ipvs_srv_t *new_ipvs_srv = NULL;
                
				new_ipvs_srv = (ipvs_srv_t *)zbx_malloc(new_ipvs_srv, sizeof(ipvs_srv_t));
				if(new_ipvs_srv == NULL)
				{
					zabbix_log(LOG_LEVEL_WARNING, "module [lvs], func [zbx_module_ipvs_read_proc_file], malloc memory error!!!");
					return ret;
				}

				if(4 == sscanf(line, "%s\t" "%x:%x" "%s", new_ipvs_srv->protocol, &tmp1, &tmp2, new_ipvs_srv->sched_name))
				{
					tmp3 = ntohl(tmp1);
					inet_ntop(AF_INET, &tmp3, new_ipvs_srv->addr, 16);
					zbx_snprintf(new_ipvs_srv->port, 10, "%u", tmp2);
					/* for dev
					zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_read_proc_file], memory postion [%p], add srv [%s %s %s %s]", new_ipvs_srv, new_ipvs_srv->protocol, new_ipvs_srv->addr, new_ipvs_srv->port, new_ipvs_srv->sched_name);
					*/
					new_ipvs_srv->next = ipvs_srv_list;
					ipvs_srv_list = new_ipvs_srv;

					ipvs_srv_list->ipvs_rs_list = NULL;
				}
			}

			if (strncmp(line, "  ->", 4) == 0 && strncmp(line, "  -> RemoteAddress", 18) != 0 )
			{
				ipvs_rs_t *new_ipvs_rs = NULL;

				new_ipvs_rs = (ipvs_rs_t*)zbx_malloc(new_ipvs_rs, sizeof(ipvs_rs_t));
				if(new_ipvs_rs == NULL)
				{
					zabbix_log(LOG_LEVEL_WARNING, "module [lvs], func [zbx_module_ipvs_read_proc_file], malloc memory error!!!");
					return ret;
				}

				if (6 == sscanf(line, "\t%*s" "%x:%x" "\t%s" "\t%d" "\t" ZBX_FS_UI64 "\t" ZBX_FS_UI64,
					&tmp1,
					&tmp2,
					new_ipvs_rs->fwd,
					&(new_ipvs_rs->weight),
					&(new_ipvs_rs->activeconns),
					&(new_ipvs_rs->inactconns)))
				{
					tmp3 = ntohl(tmp1);
					inet_ntop(AF_INET, &tmp3, new_ipvs_rs->addr, 16);
					zbx_snprintf(new_ipvs_rs->port, 10, "%u", tmp2);
					/* for dev
					zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_read_proc_file], memory postion [%p], add rs [%s %s %s %d " ZBX_FS_UI64 " " ZBX_FS_UI64 "]", new_ipvs_rs, new_ipvs_rs->addr, new_ipvs_rs->port, new_ipvs_rs->fwd, new_ipvs_rs->weight, new_ipvs_rs->activeconns, new_ipvs_rs->inactconns);
					*/
					new_ipvs_rs->next = ipvs_srv_list->ipvs_rs_list;
					ipvs_srv_list->ipvs_rs_list = new_ipvs_rs;
				}

			}
		}

		zbx_fclose(f);
		ret = SYSINFO_RET_OK;
	}
    
	return ret;
}

int	zbx_module_ipvs_free_memory()
{
	/* free srv list */
	ipvs_srv_t	*this_ipvs_srv;
	ipvs_srv_t	*next_ipvs_srv;
	ipvs_rs_t	*this_ipvs_rs;
	ipvs_rs_t	*next_ipvs_rs;

	this_ipvs_srv = ipvs_srv_list;

	while(this_ipvs_srv != NULL)
	{
		next_ipvs_srv = this_ipvs_srv->next;

		this_ipvs_rs = this_ipvs_srv->ipvs_rs_list;
		while (this_ipvs_rs != NULL)
		{
			next_ipvs_rs = this_ipvs_rs->next;
			/* for dev
			zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_free_memory], free memory [%p]", this_ipvs_rs);
			*/
			zbx_free(this_ipvs_rs);
			this_ipvs_rs = next_ipvs_rs;
		}
		/* for dev
		zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_free_memory], free memory [%p]", this_ipvs_srv);
		*/
		zbx_free(this_ipvs_srv);
		this_ipvs_srv = next_ipvs_srv;
	}

	ipvs_srv_list = NULL;

	return SYSINFO_RET_OK;
}

int	zbx_module_ipvs_discovery(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct zbx_json	j;

	ipvs_srv_t	*temp_ipvs_srv;
	ipvs_rs_t	*temp_ipvs_rs;

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
        
	if(SYSINFO_RET_OK == zbx_module_ipvs_read_proc_file())
	{
		for (temp_ipvs_srv = ipvs_srv_list; temp_ipvs_srv != NULL; temp_ipvs_srv = temp_ipvs_srv->next)
		{
			for (temp_ipvs_rs = temp_ipvs_srv->ipvs_rs_list; temp_ipvs_rs != NULL; temp_ipvs_rs = temp_ipvs_rs->next)
			{
				/* for dev
				zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_discovery], add rs [%s %s %s -> %s %s]", temp_ipvs_srv->protocol, temp_ipvs_srv->addr, temp_ipvs_srv->port, temp_ipvs_rs->addr, temp_ipvs_rs->port);
				*/
				zbx_json_addobject(&j, NULL);
				zbx_json_addstring(&j, "{#IPVSPTL}", temp_ipvs_srv->protocol, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring(&j, "{#IPVSVIP}", temp_ipvs_srv->addr, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring(&j, "{#IPVSVPORT}", temp_ipvs_srv->port, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring(&j, "{#IPVSRS}",  temp_ipvs_rs->addr, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring(&j, "{#IPVSRSPORT}",  temp_ipvs_rs->port, ZBX_JSON_TYPE_STRING);
				zbx_json_close(&j);
			}
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse proc file failed"));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	zbx_module_ipvs_free_memory();

	return SYSINFO_RET_OK;
}

int	zbx_module_ipvs_activeconns(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	*protocol, *ip, *port, *rsip, *rsport;
	int	find = 0;

	ipvs_srv_t	*temp_ipvs_srv;
	ipvs_rs_t	*temp_ipvs_rs;

	if (request->nparam != 5)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
		return ret;
	}

	protocol = get_rparam(request, 0);
	ip = get_rparam(request, 1);
	port = get_rparam(request, 2);
	rsip = get_rparam(request, 3);
	rsport = get_rparam(request, 4);

	if (SYSINFO_RET_OK == zbx_module_ipvs_read_proc_file())
	{
		for (temp_ipvs_srv = ipvs_srv_list; temp_ipvs_srv != NULL; temp_ipvs_srv = temp_ipvs_srv->next)
		{
			if (0 == strcmp(protocol, temp_ipvs_srv->protocol) && 0 == strcmp(ip, temp_ipvs_srv->addr) && 0 == strcmp(port, temp_ipvs_srv->port))
			{
				for (temp_ipvs_rs = temp_ipvs_srv->ipvs_rs_list; temp_ipvs_rs != NULL; temp_ipvs_rs = temp_ipvs_rs->next)
				{
					if (0 == strcmp(rsip, temp_ipvs_rs->addr) && 0 == strcmp(rsport, temp_ipvs_rs->port)) 
					{
						find = 1;
						SET_UI64_RESULT(result, temp_ipvs_rs->activeconns);
						ret = SYSINFO_RET_OK;
					}
				}
			}
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse proc file failed"));
	}

	zbx_module_ipvs_free_memory();

	if (find != 1)
	{
		zabbix_log(LOG_LEVEL_WARNING, "module [ipvs], func [zbx_module_ipvs_activeconns], can't find key [%s %s:%s %s:%s]", protocol, ip, port, rsip, rsport);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported key [%s %s:%s %s:%s]", protocol, ip, port, rsip, rsport));
	}

	return ret;
}

int	zbx_module_ipvs_inactconns(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	*protocol, *ip, *port, *rsip, *rsport;
	int	find = 0;

	ipvs_srv_t	*temp_ipvs_srv;
	ipvs_rs_t	*temp_ipvs_rs;

	if (request->nparam != 5)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
		return ret;
	}

	protocol = get_rparam(request, 0);
	ip = get_rparam(request, 1);
	port = get_rparam(request, 2);
	rsip = get_rparam(request, 3);
	rsport = get_rparam(request, 4);

	if (SYSINFO_RET_OK == zbx_module_ipvs_read_proc_file())
	{
		for (temp_ipvs_srv = ipvs_srv_list; temp_ipvs_srv != NULL; temp_ipvs_srv = temp_ipvs_srv->next)
		{
			if (0 == strcmp(protocol, temp_ipvs_srv->protocol) && 0 == strcmp(ip, temp_ipvs_srv->addr) && 0 == strcmp(port, temp_ipvs_srv->port))
			{
				for(temp_ipvs_rs=temp_ipvs_srv->ipvs_rs_list; temp_ipvs_rs != NULL; temp_ipvs_rs = temp_ipvs_rs->next)
				{
					if (0 == strcmp(rsip, temp_ipvs_rs->addr) && 0 == strcmp(rsport, temp_ipvs_rs->port)) 
					{
						find = 1;
						SET_UI64_RESULT(result, temp_ipvs_rs->inactconns);
						ret = SYSINFO_RET_OK;
					}
				}
			}
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse proc file failed"));
	}

	zbx_module_ipvs_free_memory();

	if (find != 1)
	{
		zabbix_log(LOG_LEVEL_WARNING, "module [ipvs], func [zbx_module_ipvs_inactconns], can't find key [%s %s:%s %s:%s]", protocol, ip, port, rsip, rsport);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported key [%s %s:%s %s:%s]", protocol, ip, port, rsip, rsport));
	}

	return ret;
}

int	zbx_module_ipvs_ping(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	FILE	*f;
	char	line[MAX_STRING_LEN];
	int	c = 0;

	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		while (NULL != fgets(line, sizeof(line), f))
		{
			if (strncmp(line, "TCP", 3) == 0 || strncmp(line, "UDP", 3) == 0)
			{
				c++;
			}
		}
		zbx_fclose(f);
	}

	/* for dev
	zabbix_log(LOG_LEVEL_INFORMATION, "module [lvs], func [zbx_module_ipvs_ping], ipvs service count [%d]", c);
	*/

	if (c > 0)
	{
		SET_UI64_RESULT(result, 1);
	}
	else
	{
		SET_UI64_RESULT(result, 0);
	}

	return SYSINFO_RET_OK;

}

int	zbx_module_ipvs_version(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	line[MAX_STRING_LEN];
	FILE	*f;
	char	ipvs_version[MAX_STRING_LEN];

	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		if (NULL != fgets(line, sizeof(line), f))
		{
			if(1 == sscanf(line, "%*s" "%*s" "%*s" "%*s" "%s", ipvs_version))
			{
				SET_STR_RESULT(result, zbx_strdup(NULL, ipvs_version));
				ret = SYSINFO_RET_OK;
			}
			else
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse first line failed"));
			}
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Can't get first line"));
		}
		zbx_fclose(f);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Can't open file /proc/net/ip_vs"));
	}

	return ret;
}

int	zbx_module_ipvs_hash_table_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	zbx_uint64_t	hash_table_size;    /* ipvs hash table size */
	int		ret = SYSINFO_RET_FAIL;
	char		line[MAX_STRING_LEN];
	FILE		*f;

	if (NULL != (f = fopen("/proc/net/ip_vs", "r")))
	{
		if (NULL != fgets(line, sizeof(line), f))
		{
			if(1 == sscanf(line, "%*[^=]" "=" ZBX_FS_UI64 "%*s", &hash_table_size))
			{
				SET_UI64_RESULT(result, hash_table_size);
				ret = SYSINFO_RET_OK;
			}
			else
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Parse first line failed"));
			}
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Can't get first line"));
		}
		zbx_fclose(f);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Can't open file /proc/net/ip_vs"));
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
