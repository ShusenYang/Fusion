/**
 * \file
 *         The default implementation of bcp_queue (see \ref bcp_queue.h). This
 *         implementation uses linked list data structure to store data packets.
 *         The current scheduling is LIFO.
 */
#include "bcp_queue.h"
#include "bcp.h"
#include <stdbool.h>
#include <string.h>
#include "lib/list.h"
#include "lib/memb.h"
#include "net/rime.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


static void bcp_queue_print(struct bcp_queue *s);

void bcp_queue_init(void *c){
    //Setup BCP
    struct bcp_conn * bcp_c = (struct bcp_conn *) c;
    bcp_c->packet_queue.list = &(bcp_c->packet_queue_list);
    bcp_c->packet_queue.bcp_connection = c;
    
    list_init(bcp_c->packet_queue_list);
    PRINTF("DEBUG: Bcp Queue has been initialized \n");
    
    /**
     * As seen, the actual memory allocation for the queue has to be performed 
     * separately by another component (the default implementation is BCP_Queue_Allocater. 
     * This will provide a great flexiablity in terms of extending the queue items and provide a customized header.
     */
}

struct bcp_queue_item * bcp_queue_top(struct bcp_queue *s){
    return list_head(*s->list);
}

struct bcp_queue_item * bcp_queue_next(struct bcp_queue *s, struct bcp_queue_item *i){
     //bcp_queue_print(s);
    return list_item_next(i);
     
}

struct bcp_queue_item * bcp_queue_element(struct bcp_queue *s, uint16_t index){
    /**
     * TODO: The complexity of this fuction is very high O(N^2)
     */
    struct bcp_queue_item * i = bcp_queue_top(s);
    int j = 0;
    
   
    for(i =  bcp_queue_top(s); i != NULL; i= list_item_next(i)){
        if(j == index){
            return i;
        }
        j++;
    }

return NULL;    
}

void bcp_queue_remove(struct bcp_queue *s, struct bcp_queue_item *i){
   PRINTF("DEBUG: Removing an item from the packet queue\n");
   //Null is not allowed here
   if(i != NULL) {
    list_remove(*s->list, i);
    memb_free(s->memb, i);
  }else{
       PRINTF("ERROR: Passed queue item record cannot be removed from the packet queue\n");
  }
   
 // bcp_queue_print(s);
}

void bcp_queue_pop(struct bcp_queue *s){
    PRINTF("DEBUG: Removing the first item from the packet queue\n");
    struct bcp_queue_item *  i = bcp_queue_top(s);
    bcp_queue_remove(s, i);
   // bcp_queue_print(s);
}

int bcp_queue_length(struct bcp_queue *s){
    return list_length(*s->list);
}

struct bcp_queue_item * bcp_queue_push(struct bcp_queue *s, struct bcp_queue_item *i){
    struct bcp_queue_item * newRow;
    
    //Make sure the queue is not full
    int current_queue_length =  bcp_queue_length(s);
     if(current_queue_length + 1 > MAX_PACKET_QUEUE_SIZE){
        PRINTF("ERROR: Packet Queue is full, a new packet will be dropped \n");
        return NULL;
    }
    
    // Allocate a memory block for the new record
    newRow = memb_alloc(s->memb);
  
     if(newRow == NULL) {
         PRINTF("ERROR: memory cannot be allocated for a bcp_queue_item record. Queue length=%d \n", current_queue_length);
         return NULL;
     }
    
    
    //Sets the fields of the new record
    memcpy(newRow, i, i->hdr.packet_length);
    newRow->next = NULL;
    newRow->hdr.bcp_backpressure = 0;
    newRow->hdr.packet_length = i->hdr.packet_length;   
    
    
    //Add the row to the queue
    list_push(*s->list, newRow);
    
    PRINTF("DEBUG: Pushing a new data packet to the packet queue\n");
    //if(newRow ->hdr.origin.u8[0] == 250)
    //  bcp_queue_print(s);
    
     return newRow;
    
}


void bcp_queue_clear(struct bcp_queue *s){
  //For every stored record
  while(bcp_queue_top(s) != NULL) {
    bcp_queue_pop(s);
  }
  
  PRINTF("DEBUG: Packet Queue has been cleared\n");
  
}

static void bcp_queue_print(struct bcp_queue *s){
    #if DEBUG
        struct bcp_queue_item * i;
        int j = 0;
        for(i =  bcp_queue_top(s); i != NULL; i= list_item_next(i)){
            printf("DEBUG: Queue item#%d=%p node[%d].[%d] result=%x\n", j++, i, i->hdr.origin.u8[0], i->hdr.origin.u8[1], i->data);
        }
    #endif
}