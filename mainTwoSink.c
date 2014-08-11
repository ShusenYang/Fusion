#include "contiki.h"
#include "net/rime.h"
#include "bcp.h"
#include "bcp_queue.h"
#include "sensing_control.h"
#include "lpm.h"
#include "dev/serial-line.h"
#include <stdio.h>
#include "lib/random.h"


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


//Slot timer 

clock_time_t time_ee = CLOCK_SECOND * SLOT_DURATION;

struct ctimer send_data_timer; 
static uint32_t countPack = 0;
  //Callback for receiving a message from bcp
  static void recv_bcp(struct bcp_conn *c, rimeaddr_t * from)
{
     // printf("%%%FSDFSDFS\n");
     // printf("rec_packet=%d \n", ++countPack);
}
  
  void sent_bcp(struct bcp_conn *c){
    
      
  }
  
  
  //Send function
  void sn(void* v){
      //PRINTF("DEBUG: Slot Timer has been triggered\n");
      
   
      
/*
      uint32_t rx = sensing_rate(&(bcp.packet_queue));
      PRINTF("Rx=%d \n", rx );
      
*/
      int len = bcp_queue_length(&(bcp.packet_queue));
      printf("len=%d \n", len);

/*
      int i;
      
      //Sending date based on the current sensing rate
      for(i = 0; i < rx; i++){
           uint16_t d = 258;
           packetbuf_copyfrom(&d, 2);
           bcp_send(&bcp); 
      }
*/
      
      ctimer_set(&send_data_timer, time_ee, sn, NULL);
      
  }
  

  static const struct bcp_callbacks bcp_callbacks = { recv_bcp, sent_bcp };
  static unsigned short solarRnd = 0;
  
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
  addr.u8[1] = 0;
  
  char isSink = rimeaddr_cmp(&addr, &rimeaddr_node_addr);

/*
  if(!isSink){
       //Set the second sink node
        addr.u8[0] = 2;
        addr.u8[1] = 0;
        isSink = rimeaddr_cmp(&addr, &rimeaddr_node_addr);
  }
  
   if(!isSink){
       //Set the second sink node
        addr.u8[0] = 3;
        addr.u8[1] = 0;
        isSink = rimeaddr_cmp(&addr, &rimeaddr_node_addr);
  }
  
   if(!isSink){
       //Set the second sink node
        addr.u8[0] = 4;
        addr.u8[1] = 0;
        isSink = rimeaddr_cmp(&addr, &rimeaddr_node_addr);
  }
*/

  
  if(isSink){
      bcp_set_sink(&bcp, true);
     
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
 
   for(;;) {
     PROCESS_YIELD();
     if(ev == serial_line_event_message) {
       uint16_t energy = (uint16_t) atoi(data);
       //printf("before input solar: %d\n", energy);
       
       energy -= (energy*(solarRnd/100));
       printf("input solar: %d\n", energy);
       
       printf("battery_level=%d\n", lpm_get_battery_level());  
       lpm_set_input(energy);
       printf("energy_budget=%d\n", lpm_get_energy_budget());
     }
   }
   PROCESS_END();
 }