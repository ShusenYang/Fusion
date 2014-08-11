#include "contiki.h"
#include "net/rime.h"
#include "bcp.h"
#include "bcp_queue.h"
#include "sensing_control.h"
#include "lpm.h"
#include "dev/serial-line.h"
#include <stdio.h>
#include "lib/random.h"
#include "solarTrace.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif




//Veriables 
static struct bcp_conn bcp;
static rimeaddr_t addr;
static unsigned short solarCounter = 0; //Count the current time slot
static unsigned short solarRnd = 0; //Solar Round Number; To generate +- 100% different solar input between nodes

//Slot timer 

clock_time_t time_ee = CLOCK_SECOND * SLOT_DURATION;

struct ctimer send_data_timer; 
static uint32_t countPack = 0;
  //Callback for receiving a message from bcp
  static void recv_bcp(struct bcp_conn *c, rimeaddr_t * from)
{
     // printf("%%%FSDFSDFS\n");
      printf("rec_packet from node[%d].[%d]. Counter=%d\n", from->u8[0], from->u8[1], ++countPack);
}
  
  void sent_bcp(struct bcp_conn *c){
    
      
  }
  
  
  //Send function
  void sn(void* v){
      //PRINTF("DEBUG: Slot Timer has been triggered\n");
  
      int len = bcp_queue_length(&(bcp.packet_queue));
      printf("len=%d for timeslot=%d\n", len, solarCounter);
      countPack = 0;
      
      
      //Fetch the solar data
      
       unsigned short energy = solarTrace[solarCounter++];
       if(solarCounter > 8638){
           printf("##############################################\n");
           printf("#################END##########################\n");
           printf("##############################################\n");
           bcp_close(&bcp);
           return;
       }
       //memcpy(&solarTrace[solarCounter++], &energy, 2);
       
       printf("even before input solar: %d\n", energy);
        
       energy = energy * 34 * 0.071 * 0.1;
       
       printf("before input solar: %d\n", energy);
       
       unsigned short energyDiffer = ((energy*solarRnd)/100);
       
       energy = energy - energyDiffer;
       printf("input solar: %d\n", energy);
       
       printf("battery_level=%ld\n", lpm_get_battery_level());  
       lpm_set_input(energy);
       printf("energy_budget=%d\n", lpm_get_energy_budget());
       

      
      ctimer_set(&send_data_timer, time_ee, sn, NULL);
      
  }
  

  static const struct bcp_callbacks bcp_callbacks = { recv_bcp, sent_bcp };

  
/*---------------------------------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS(serial_process, "Serial line test process");
AUTOSTART_PROCESSES(&main_process, &serial_process);

PROCESS_THREAD(main_process, ev, data)
{
  
  PROCESS_BEGIN();
    
  bcp_open(&bcp, 146, &bcp_callbacks);
  solarRnd = 1;
  solarRnd += random_rand() % (49);
  printf("solarRnd=%d\n",solarRnd);
  //Set the sink node
  addr.u8[0] = 1;
  addr.u8[1] = 245; //Based on rennes.senslab.info
  
  
  char isSink = rimeaddr_cmp(&addr, &rimeaddr_node_addr);

  if(isSink){
      bcp_set_sink(&bcp, true);
       ctimer_set(&send_data_timer, time_ee, sn, NULL);
  }else{
      
       ctimer_set(&send_data_timer, time_ee, sn, NULL);
  }  
  
  PROCESS_END();
}


/**
 * This process is called whenever new line is written to the serial port
 */
PROCESS_THREAD(serial_process, ev, data)
 {
   PROCESS_BEGIN();
 
   
   PROCESS_END();
 }