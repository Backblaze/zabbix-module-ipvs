Description
===========

This directory contains a sample module, which extends functionality of Zabbix Agent. 

Status
======

This module is production ready.

Installation
============

```bash

	$ git clone https://github.com/cnshawncao/zabbix-module-ipvs.git
	$ cp -r zabbix-module-ipvs zabbix-x.x.x/src/modules/	# zabbix-x.x.x is zabbix version
```

1. run 'make' to build it. It should produce lvs.so.

1. copy lvs.so to the module directory, like LoadModulePath=/etc/zabbix/modules

1. change config file add line : LoadModule=lvs.so

1. restart zabbix_agent daemon

1. import the zabbix template *zbx_template_ipvs_active.xml*

Synopsis
========

**key:** *ipvs.stats[cps]*

**key:** *ipvs.stats[inpps]*

**key:** *ipvs.stats[outpps]*

**key:** *ipvs.stats[inbps]*

**key:** *ipvs.stats[outbps]*
