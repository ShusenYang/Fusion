/**
 * \file
 *         Default implementation of local power management component. This implementation
 *         is based on PHD-27 specification.
 */

#include "contiki.h"
#include "lpm.h"
#include "fusion_config.h"
#include <stdio.h>


#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


void lpm_set_unusedEnergy(uint16_t energy){
    return;
   
}
void lpm_set_input(uint16_t solar_energy){
    return;    
}


uint16_t lpm_get_energy_budget(){
    printf("test\n");
    uint16_t r = 200;
    return r;
}

uint32_t lpm_get_battery_level(){
    return (uint32_t)600000;
}
