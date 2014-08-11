CONTIKI = /senslab/users/tahir/iot-lab/wsn430/OS/Contiki
#/home/user/contiki-2.7

CONTIKI_PROJECT = main

CONTIKI_SOURCEFILES += bcp.c bcp_routing_table.c bcp_queue_lifo.c hop_counter.c fusion_weight_estimator.c fusion.c sensing_control.c lpm_jsac.c
#CONTIKI_SOURCEFILES += bcp.c bcp_routing_table_dag.c bcp_queue_lifo.c hop_counter.c fusion_weight_estimator.c fusion.c sensing_control.c lpm.c

PROJECT_SOURCEFILES += common-config.c

#Load our own project-conf to employ nullrdc driver
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

all: $(CONTIKI_PROJECT)
#include ./bcp/Makefile.bcp
include $(CONTIKI)/Makefile.include 
