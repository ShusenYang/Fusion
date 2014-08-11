#include "fusion.h"
#include "bcp.h"
#include "bcp_queue.h"
#include "bcp_queue_allocator.h" //To customize the queue item 
#include "bcp_extend.h" //To extend BCP operations
#include "fusion_energy_control.h" //To get energy budgets for sending and fusion
#include "lib/random.h"

#define NUM_CID 2

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


/**
 * \brief      A structure for the header part of bcp packets
 */
struct fusion_packet_header {
    struct bcp_packet_header bcp_header;
    char fused; //Flag to indicate whether the the message has been fused in the 
                //current node or node. If yes, other fusion operations are not 
                //allowed to be performed on this message
    
    uint16_t CID; //Correlation ID. Every node belongs to a group. 
};

/**
 * \brief      A structure for the records of the packet queue
 */
struct fusion_queue_item {
  //Linked list
  struct fusion_queue_item *next;

  
  /**
   * The data section
   */
  char data[MAX_USER_PACKET_SIZE]; //Data
  /**
   * The header section
   */
  struct fusion_packet_header hdr; //Header
  
};




//Memory allocation for the routing table. This is defined here because 
MEMB(fusion_packet_queue_memb, struct fusion_queue_item, MAX_PACKET_QUEUE_SIZE);
static unsigned short CID = 255;


/**
 * \return the Correlation ID (CID) for this node. Each node should belong to a group,
 * and each group has one CID.
 */
static uint16_t getCID(){
    
    if(CID == 255){
        CID = random_rand() % NUM_CID;
        CID += 1; //To start from one   
        PRINTF("DEBUG: CID for this node is %d \n", CID);
    }
    return CID;
}

/**
 * \return true if the given packet is a fusion packet. Otherwise, false.
 */
static bool isFusionPacket(struct bcp_queue_item* itm){
    
    struct fusion_queue_item * fItm = ( struct fusion_queue_item *) itm;
    
    //If it is a fusion packet 
    return (fItm->hdr.bcp_header.origin.u8[0] == 250 
            && fItm->hdr.bcp_header.origin.u8[1] == 250);
}

struct bcp_queue_item* beforeSending(struct bcp_conn *c,  struct bcp_queue_item* itm){
    PRINTF("DEBUG: Before Sending Data \n");
    
    //Check the sending energy budget
    if(get_sending_budget()==0)
        return NULL;
    PRINTF("DEBUG: Sending Budget = %d\n",get_sending_budget() );
    itm->hdr.packet_length = sizeof(struct fusion_queue_item);
    
    //Update the energy consumption for the sending
    set_consumed_sending_budget(1);
    
    return itm;
}
void onUserRequest(struct bcp_conn *c, struct bcp_queue_item* itm){  
    struct fusion_queue_item *i = (struct fusion_queue_item*) itm;
    i->hdr.CID = getCID();
    i->hdr.fused = 0;
    //Overwrite data length since the fusion item data structure is different from the default queue item data structure 
    i->hdr.bcp_header.packet_length = sizeof(struct fusion_queue_item);
    
   // PRINTF("DEBUG: Setting the CID(%d) for the new message. \n", getCID());
}
void afterSending(struct bcp_conn *c,  struct bcp_queue_item* itm){
    PRINTF("DEBUG: After Sending Data \n");
}

static struct fusion_queue_item * getFusionQueueItem(struct bcp_queue *q, uint16_t i){
    return (struct fusion_queue_item*) bcp_queue_element(q, i);
}

static short fusionRule(struct fusion_queue_item** fusionList, int fusionItemCounter){
    //Execute the fusion function on the fusion list
    //In this implementation, the fusion function is max
    return 10; //Just for testing
    int m;
    int max = 0;
    for(m =0 ; m < fusionItemCounter; m++){
        if (max < fusionList[m]->data){
            max = fusionList[m]->data;
        }
    }
    
    return max;
}

static void removeFusedPackets(struct bcp_queue * q, void** fusionList, int len){
     int m;
     for(m =0 ; m < len; m++){
       struct fusion_queue_item * itm = (struct fusion_queue_item *) fusionList[m];
       
       PRINTF("DEBUG: Removing fused packet coming from node[%d].[%d] p=%p\n", 
                     itm->hdr.bcp_header.origin.u8[0],
                     itm->hdr.bcp_header.origin.u8[1],
                     itm);
       bcp_queue_remove(q, itm);
       fusionList[m] = NULL;
     }
}


void performFusion(struct bcp_queue * q ){
        
        int len = bcp_queue_length(q);        
        void* fusionList[MAX_PACKET_QUEUE_SIZE];
        int fusionItemCounter;
        int i, j, k, m, ignore, perFusion;
        clock_time_t fusionDelay;
        uint16_t eCID;
        
        struct fusion_queue_item * e;
        struct fusion_queue_item * eNested;
        struct fusion_queue_item * top;
        
        
        //Get the top item
        top = bcp_queue_top(q);
        
        
            
        
        PRINTF("DEBUG: Performing fusion on the queue. Current queue length=%d\n", len);
        
        /*
         * The complexity of this function is O(N^(NUM_CID)) 
         *
         */
        
        for(i = 1; i < NUM_CID+1; i++){ //CID loop     
            eCID = i;
            fusionItemCounter = perFusion = fusionDelay = 0;
            eNested = top;
            
            for(j=0; j < len; j++){
               
                if(j!=0) //If it is not top, get the next packet in the queue
                    eNested =  bcp_queue_next(q, eNested);
              
                if(eNested == NULL || get_fusion_budget() == 0)
                    break;
                 //printf("j=%d CID=%d\n", j, eCID);
                // printf("node[%d] p=%p fused=%d\n", eNested->hdr.bcp_header.origin.u8[0], eNested,  eNested->hdr.fused);
                   
                if(eNested->hdr.CID == eCID && eNested->hdr.fused == 0){ 
                     
                    //Version 1 - if I am not the source, fused me 
                    if(rimeaddr_cmp(&eNested->hdr.bcp_header.origin, &rimeaddr_node_addr))
                        continue;
                    
                    if(isFusionPacket(eNested)){
                        uint16_t f = 0;
                        memcpy(&f, &eNested->data, 2);
                        perFusion += f; 
                    }
                    
                
                    
                    //If all required conditions passed, add queue item to the fusion list;
                    fusionList[fusionItemCounter] = eNested;
                    fusionDelay += eNested->hdr.bcp_header.delay;
                    fusionItemCounter++;
                    
                    if(fusionItemCounter > 2)
                       set_consumed_fusion_budget(1);
                    else if(fusionItemCounter == 2)
                       set_consumed_fusion_budget(2); //To avoid fusion where only one packet exists 
               } //if same CID   
            } //j loop
           
            //Execute the fusion rule on the fusion list
            short result = fusionRule(fusionList, fusionItemCounter);
             
            if(fusionItemCounter > 1){
                printf("fused=%d\n", fusionItemCounter);
                //Remove the packets after the fusion 
                removeFusedPackets(q,&fusionList, fusionItemCounter);
                //Add the fusion packet to the list 
                struct fusion_queue_item fusionPacket;
                fusionPacket.hdr.fused = 1;
                fusionPacket.hdr.CID = eCID;
                fusionPacket.hdr.bcp_header.packet_length = sizeof(struct fusion_queue_item);
                fusionPacket.hdr.bcp_header.origin.u8[0] = 250;
                fusionPacket.hdr.bcp_header.origin.u8[1] = 250;
                fusionPacket.hdr.bcp_header.delay = fusionDelay/fusionItemCounter; //Average delay
                fusionPacket.hdr.bcp_header.lastProcessTime = clock_time();
                uint16_t totalFusion =  perFusion + fusionItemCounter; //The data is actual the number of packets fused in this fusion packet
                memcpy(&fusionPacket.data, &totalFusion,2 );
                
                
                bcp_queue_push(q, &fusionPacket);
                //PRINTF("DEBUG: Fusion packet was added to the queue \n");
          }
   } //i loop
          PRINTF("DEBUG: Fusion has been done i=%d j=%d \n", i, j);
 }



void onReceiving(struct bcp_conn *c, struct bcp_queue_item* itm){
    PRINTF("DEBUG: On Receiving Data \n");
   
    struct fusion_queue_item * fItm = ( struct fusion_queue_item *) itm;
    
    fItm->hdr.bcp_header.packet_length = sizeof(struct fusion_queue_item);
    
    //If it is a fusion packet 
    if(isFusionPacket(itm)){
         
        PRINTF("DEBUG: A fusion Packet has been received. Changing the fused flag to zero \n");
        fItm->hdr.fused = 0;
    }

}

void prepareDataPacket(struct bcp_conn *c,  struct bcp_queue_item* itm){
    
    PRINTF("DEBUG: On prepareDataPacket \n");
    
    struct fusion_queue_item * fItm = ( struct fusion_queue_item *) itm;
    
    
    //struct bcp_queue * q = &(c->packet_queue);
    
   //If it is not a sink and has a fusion budget
    //if(c->isSink == 0 && get_fusion_budget()!=0){
   //     performFusion(q);  
    //} 
}


static const struct bcp_extender ex = {&prepareDataPacket, &beforeSending, &afterSending, &onReceiving, &onUserRequest};


void bcp_queue_allocator_init(struct bcp_conn *c){
 
    c->packet_queue.memb = &fusion_packet_queue_memb;
    memb_init(&fusion_packet_queue_memb);    
    c->ce = &ex; //Set the custom BCP extender  
}



