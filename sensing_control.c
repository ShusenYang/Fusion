#include "bcp.h"
#include "bcp_queue.h"
#include "sensing_control.h"
#include "fusion_config.h"

static int32_t v = SENSING_V; 
static int32_t rMax = SENSING_rMax;
static int32_t bigerLine = 0;
static int32_t sensing_cost = 0;
static int32_t rEnergyMax = 0;

void sensing_setBigerLine(int p){
    bigerLine = p;
}

void sensing_setCost(int cost){
    sensing_cost = cost;
    rEnergyMax = lpm_get_energy_budget()/sensing_cost;
    
    if(rMax < rEnergyMax)
        rEnergyMax = rMax;
    
}

uint16_t sensing_rate(struct bcp_queue *s){
    
     //Returns the sensing rate based on the current queue backlog
    int len = bcp_queue_length(s);
     
    int32_t low_bundle = bigerLine*sensing_cost+(int32_t)len;
    //printf("bigerLine*sensing_cost+len=%d\n", low_bundle);
    
    if( low_bundle == 0)  
       return 0;
    
    int32_t comp = v/(int32_t)low_bundle;
    comp -= 1;
    //printf("v=%d\n",  v);
    //printf("comp=%ld\n", comp);    
    
    if(comp < (int32_t) 0){
        return 0;
    }
    
    if(comp > rEnergyMax){
        return (uint16_t)rEnergyMax;
    }
   
    return  (uint16_t)comp;   
}
