# zabbix-module-ipvs
This directory contains a sample module, which extends functionality of Zabbix
 Agent. 

Run 'make' to build it. It should produce lvs.so.

Using:

1. copy lvs.so to the module directory, like LoadModulePath=/etc/zabbix/modules

2. change config file add line : LoadModule=lvs.so

3. restart zabbix_agent daemon


key: ipvs.discovery
value:
{
    "data":[
        {
            "{#IPVSPTL}":"Protocol",
            "{#IPVSVIP}":"IPVS VIP",
            "{#IPVSVPORT}":"IPVS VPORT",
            "{#IPVSRS}":"IPVS RealServer",
            "{#IPVSRSPORT}":"IPVS RealServer Port"},
        {
            ......
            ......}]}
    
key: ipvs.activeconns[IPVSPTL,IPVSVIP,IPVSVPORT,IPVSRS,IPVSRSPORT]
key: ipvs.inactconns[IPVSPTL,IPVSVIP,IPVSVPORT,IPVSRS,IPVSRSPORT]
