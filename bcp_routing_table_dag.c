/**
 * \file
 *         default implementation of routing table.
 *
 */

#include "bcp_routing_table.h"
#include "bcp.h"
#include "fusion_config.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif



struct parent_estiblished_msg {
 
};

//Timer for forwardable flag
clock_time_t time_fe = CLOCK_SECOND * 10;
struct ctimer forwardable_timer; 
static struct routingtable_item * parents[NUM_PARENTS];
struct runicast_conn unicast_conn;
static uint16_t uni_channel = 32; //Temp channel
static struct bcp_conn * active_bcp;
static int parent_counter;


static void recv_from_unicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t sq)
{
    struct routingtable_item *i;
   
    i = routing_table_find(&active_bcp->routing_table,from);
    PRINTF("DEBUG: Receiving Parent estiblishment message from node[%d].[%d]\n",
            from->u8[0], from->u8[1]);
    if(i != NULL){
        i->forwardable = 250; //Meaning this neighbor is a child and data should not be forwarded to him
    }
    
    //print_routingtable(&active_bcp->routing_table);
}

static void sent_from_unicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t atmp){
    struct routingtable_item * nested = NULL; 
    parent_counter++;
    PRINTF("DEBUG: Parent estiblishment message sent to node[%d].[%d]\n", from->u8[0], from->u8[1]);
     if(parent_counter < NUM_PARENTS){
        nested = parents[parent_counter];
        if(nested != NULL){
           nested->forwardable = 1;
           
           prepareMessage();
           runicast_send(&unicast_conn,&nested->neighbor,10);
        }
     }
}

static const struct runicast_callbacks uni_callbacks = { recv_from_unicast, sent_from_unicast };

void prepareMessage(){
     packetbuf_clear();
     packetbuf_set_datalen(sizeof(struct parent_estiblished_msg));
}
/**
 * \breif updates the forwardable flag for all neighbors. If a neighbor has a 
 * forwardable equals to one, the node can forward data to the neighbor (i.e the
 * neighbor is one of the node's parents).
 * 
 * 
 * 
 */
void updateForwardable(void* v){
    //Get the neighbors with forwardable flag != 1
    struct bcp_conn * bcp_c = (struct bcp_conn *) v;
    
    struct routingtable *t = &bcp_c->routing_table;
    int k, j;
    struct routingtable_item *i = NULL;
    struct routingtable_item * nested = NULL;
   
    //PRINTF("Update forwardable flags has been started\n");
    //Reset the parents
    for(k = 0; k < NUM_PARENTS; k++){
        parents[k] = NULL;
    }
    
   
    
    //For each neighbor  
    for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
        
        //If the neighbor is a child
        if(i->forwardable == 250){
            continue; //Since bidirectional links are not allowed in DAG
        }
        
        i->forwardable = 10; //Disable forwarding initially     
        if(i->hop_count != 0)
        //For each parent
        for(k = 0; k < NUM_PARENTS; k++){
           nested = parents[k];
           if(nested == NULL){
               parents[k] = i;
              
               break;
           }else{
               if( i->hop_count < nested->hop_count){
                   //Shift the reset of parents one down
                   for(j = NUM_PARENTS-2; j >= k; j--){
                        parents[j+1] = parents[j];
                   }
                   //Add the new parent
                  
                   parents[k] = i;
                   break;
               }
           }
        }
    }
    
    //Update the forwardable flag based on the new parent table
    parent_counter = 0;
    nested = parents[parent_counter];
    if(nested != NULL){
       nested->forwardable = 1;
       prepareMessage();
       runicast_send(&unicast_conn,&nested->neighbor,10);
    }
  
    print_routingtable(t);
 }

void routing_table_init(void *c){
    //Setup bcp
    struct bcp_conn * bcp_c = (struct bcp_conn *) c;
    bcp_c->routing_table.list = &(bcp_c->routing_table_list);
    bcp_c->routing_table.bcp_connection = c;
    //Init the list
    list_init(bcp_c->routing_table_list);
   
    PRINTF("DEBUG: Bcp routing table has been initialized \n");
    active_bcp = bcp_c;
    runicast_open(&unicast_conn, uni_channel, &uni_callbacks);
}

struct routingtable_item* routing_table_find(struct routingtable *t,
                               const rimeaddr_t * addr){
     struct routingtable_item *i = NULL;
   
    // Check for entry using binary search as number of records is usually very limited
    for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
      if(rimeaddr_cmp(&(i->neighbor), addr))
        break;
    }
   
     return i;
}

int routing_table_update_queuelog(struct routingtable *t,
                               const rimeaddr_t * addr,
                               uint16_t queuelog, uint16_t isData){
    struct routingtable_item *i;
   
    i = routing_table_find(t, addr);
    
    //No record for this neighbor address
    if(i == NULL) {
        // Allocate memory for the new record
        i = memb_alloc(t->memb);

        //Failed to allocate memory
        if(i == NULL) {
          return 0;
        }

        // Set default attributes
        i->next = NULL;
        rimeaddr_copy(&(i->neighbor), addr);
        i->backpressure = queuelog;
        
        
        //Ask weight estimator to initialize its fields 
        weight_estimator_record_init(i);
        
        //Insert the new record
        list_add(*t->list, i);
    }else{
        i->backpressure = queuelog;
    }
    
    if ((int) queuelog < 0 || queuelog > MAX_PACKET_QUEUE_SIZE ){
        i->backpressure =  MAX_PACKET_QUEUE_SIZE;
    }
    
    //printf("isDATA=%d \n", isData);
        
    if(isData == 1){
    //To avoid loop, we will set the forwardable flag so that this node
    //will not forward any data to this neighbor for the next ten time
    // slots 
    i->forwardable = 11; //This value is decreased by 1 at the beginning of 
                         //every time slot
    PRINTF("DEBUG: Changing queuelog for node[%d].[%d]. Mark the node as unforwardable for the next %d time slot(s) \n"
            , addr->u8[0], addr->u8[1],  i->forwardable -1);
    }

    //dbg_print_rtable(t);
    return 1;
}

int routingtable_length(struct routingtable *t)
{
  return list_length(*t->list);
}



void routingtable_clear(struct routingtable *t){
    
   struct routingtable_item *i;
   for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
       list_remove(*t->list, i);
       memb_free(t->memb, i);
   }
   
   PRINTF("DEBUG: Routing table has been cleared\n");
}


void routingtable_clearForwardable(struct routingtable *t){
   struct routingtable_item *i;
   for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
       if(i->forwardable == 1){
            list_remove(*t->list, i);
            memb_free(t->memb, i);
       }
   } 
}


rimeaddr_t* routingtable_find_routing( struct routingtable *t){
   
   int largestWeight = -32768;
   int neighborWeight;
   struct routingtable_item * largestNeightbor = NULL;
   struct routingtable_item *i;
   //For each neighbor stored 
   for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
       //If smallest weight variable is not yet set 
       neighborWeight = weight_estimator_getWeight(t->bcp_connection, i);
       //Has this neighbor smaller weight
       //Temp Test
       if(largestWeight <= neighborWeight && i->forwardable == 1){
           largestWeight = neighborWeight;
           largestNeightbor = i;
       }
   }
   //No result
   if(largestNeightbor == NULL || largestWeight < 1)
       return NULL;
  
   
    // PRINTF("DEBUG: Best neighbor to send the data packet is node[%d].[%d] \n",
    //           largestNeightbor->neighbor.u8[0],
    //           largestNeightbor->neighbor.u8[1]);
    //print_routingtable(t);
   //The rime address of the neighbor
   return (&largestNeightbor->neighbor);
}

int routing_table_update_hopCount(struct routingtable *t,
                               const rimeaddr_t * addr,
                               uint16_t hop_count){
    struct routingtable_item *i;
   
    i = routing_table_find(t, addr);
    
    //No record for this neighbor address
    if(i == NULL) {
        // Allocate memory for the new record
        i = memb_alloc(t->memb);

        //Failed to allocate memory
        if(i == NULL) {
          return 0;
        }

        // Set default attributes
        i->next = NULL;
        rimeaddr_copy(&(i->neighbor), addr);
        i->backpressure = 0;
        i->hop_count = hop_count;
        //Ask weight estimator to initialize its fields 
        weight_estimator_record_init(i);
        
        //Insert the new record
        list_add(*t->list, i);
    }else{
        i->hop_count = hop_count;
    }
    return 1;
}

struct routingtable_item* routing_table_find_shortestPath(struct routingtable *t){
    
    struct routingtable_item* result = NULL;
    uint16_t smallestHop_count = 0xffff;
    struct routingtable_item *i;
    
    //For each neighbor
    for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
        //Zero neighbor hop-count means that the neighbor's hop_count has not
        //been calculated yet
        if (i->hop_count <= smallestHop_count && i->hop_count != 0){
            result = i;
            smallestHop_count = i->hop_count;
        }
    }
    
    
    //Since this function has been called, it means the hop counter for 
    //this node has been calculated. 
    //Start the forwardable timer to choose the parent nodes
    ctimer_set(&forwardable_timer, time_fe, updateForwardable, t->bcp_connection);

    
    return result;
}
/*---------------------------------------------------------------------------*/
 void print_routingtable(struct routingtable *t)
{
  #if DEBUG
  struct routingtable_item *i;
  uint8_t numItems = 0;
  uint8_t count = 0;
  numItems = routingtable_length(t);

  PRINTF("Routing Table Contents: %d entries found\n", numItems);
  PRINTF("------------------------------------------------------------\n");
  for(i = list_head(*t->list); i != NULL; i = list_item_next(i)) {
    PRINTF("Routing table item: %d\n", count);
    PRINTF("neighbor: %d.%d\n", i->neighbor.u8[0], i->neighbor.u8[1]);
    PRINTF("backpressure: %d\n", i->backpressure);
    PRINTF("forwardable: %d\n",  i->forwardable);
    PRINTF("hop-count: %d\n",  i->hop_count);
    weight_estimator_print_item(t->bcp_connection, i);
    PRINTF("------------------------------------------------------------\n");
    count++;
  }
  #endif
}
/*---------------------------------------------------------------------------*/