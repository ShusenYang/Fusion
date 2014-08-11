#include "hop_counter.h"
#include "bcp.h"
#include "net/rime.h"
#include "net/rime/unicast.h"
#include "net/rime/broadcast.h"
#include "lib/random.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


struct hop_counter_msg {
  uint16_t hop_count; 
};

struct broadcast_conn hc_broadcast_conn;
static uint16_t hc_channel = 22; //Temp channel
static clock_time_t prepare_time = CLOCK_SECOND * 1;
static short maxSeconds = 10;
static clock_time_t hc_time;
static struct ctimer hc_timer;
static struct bcp_conn * current_bcp;
static bool isInitialized = false;


static void sent_from_broadcast(struct broadcast_conn *c, int status,
                                int transmissions);
static void recv_from_broadcast(struct broadcast_conn *c,
                                const rimeaddr_t *from);
static void send_packet(void *ptr);
static void close_phase(void *ptr);

static const struct broadcast_callbacks hc_broadcast_callbacks = {
    recv_from_broadcast,
    sent_from_broadcast };

static void sent_from_broadcast(struct broadcast_conn *c, int status,
                                int transmissions)
{
    isInitialized = true;
    hc_time = (CLOCK_SECOND * maxSeconds);
    ctimer_set(&hc_timer, hc_time, close_phase,current_bcp);
}

static void recv_from_broadcast(struct broadcast_conn *c,
                                const rimeaddr_t *from)
{
     struct hop_counter_msg c_msg;
     memcpy(&c_msg, packetbuf_dataptr(), sizeof(struct hop_counter_msg));
            
    PRINTF("DEBUG: hop-count message (hop-count=%d) has been received from node[%d].[%d].\n",
          c_msg.hop_count,
          from->u8[0], 
          from->u8[1]);
   
    //Update the routing table
    routing_table_update_hopCount(&current_bcp->routing_table, 
                                from,
                                c_msg.hop_count);
    //Reschedule my hop-count message    
    if(!isInitialized && ctimer_expired(&hc_timer)){
        hc_time = CLOCK_SECOND * (random_rand()% maxSeconds);
        ctimer_set(&hc_timer, hc_time, send_packet,current_bcp);
    }
    
}

 static void send_packet(void *ptr)
{
    struct bcp_conn *c = ptr;
    struct hop_counter_msg * m;
    struct routingtable_item * shortestPath = NULL;
    
    //Prepare packetbuf
    packetbuf_clear();
    packetbuf_set_datalen(sizeof(struct hop_counter_msg));
    //Set the packetbuf data
    m = packetbuf_dataptr();
    memset(m, 0, sizeof(struct hop_counter_msg));
    
    if(c->isSink != 1){
        shortestPath 
                    = routing_table_find_shortestPath(&current_bcp->routing_table);
        if(shortestPath==NULL){
           m->hop_count = 1; //Because zero means hop-count is not initialized yet
        }else{
           m->hop_count = shortestPath->hop_count + 1;
        }
    }else{
         m->hop_count = 1;
    }
    
    
   if(shortestPath!=NULL){
        PRINTF("DEBUG: Sending a hop count message (hop-count=%d) to 1-hop neighbors. Shortest parent node[%d].[%d]\n"
                , m->hop_count, 
                shortestPath->neighbor.u8[0],
                shortestPath->neighbor.u8[1]);
    }else{
        PRINTF("DEBUG: Sending a hop count message (hop-count=%d) to 1-hop neighbors. p=%p\n"
                , m->hop_count, shortestPath);
    }
    //Send the message
    broadcast_send(&hc_broadcast_conn);   
}

 
 static void prepare_phase(void *ptr){
     struct bcp_conn *c = ptr;
     if(c->isSink == 1){
        //Broadcast a hop counter message after a while
        hc_time = CLOCK_SECOND * (random_rand()% maxSeconds);
        ctimer_set(&hc_timer, hc_time, send_packet,c); 
    }
 }
 
 static void close_phase(void *ptr){
    //This broadcast channel is no longer required
    broadcast_close(&hc_broadcast_conn);
 }
void hop_counter_init(void *c){
    //Setup bcp
    struct bcp_conn * bcp_c = (struct bcp_conn *) c;
    current_bcp = bcp_c;
    isInitialized = false;
    //Open the broadcast channel
    broadcast_open(&hc_broadcast_conn, hc_channel, &hc_broadcast_callbacks);
    
    
    PRINTF("DEBUG: Initializing the hop counter component. \n");
    //After certain time, check whether this node is sink or not
    ctimer_set(&hc_timer, prepare_time, prepare_phase,bcp_c); 
        
}
