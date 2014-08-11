#include "contiki.h"
#include "dev/serial-line.h"
#include <stdio.h>
#include "lpm.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


 
 PROCESS(test_serial, "Serial line test process");
 AUTOSTART_PROCESSES(&test_serial);
 
 PROCESS_THREAD(test_serial, ev, data)
 {
   PROCESS_BEGIN();
 
   for(;;) {
     PROCESS_YIELD();
     if(ev == serial_line_event_message) {
       uint16_t energy = (uint16_t) atoi(data);
       //lpm_set_input(energy);
       printf("solar: %s\n", (char *)data);
       
       PRINTF("---------\n");
     }
   }
   PROCESS_END();
 }
