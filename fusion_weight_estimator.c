/**
 * \file
 *         This implementation of the weight estimator considers the 
 *         energy budget in weight calculation.
 *         
 *         The basic algorithm for this estimator is to calculate sending budget fx,y,
 *         and fusion budget during each time cycle (t). If the fx,y is less than the fusion
 *         budget, the node sends packets in t. Otherwise, the node does not performs any sending
 *         operations, just fusion.
 * 
 *         This component also provides implementation for fusion_energy_control.h which provides
 *         a very clear interface for other components to check the available  energy budget for
 *         the three operations types (sensing, fusion, and sending).
 *         
 *        
 */
#include "bcp_weight_estimator.h"
#include "fusion_energy_control.h"
#include "fusion_config.h"
#include "bcp.h"
#include "bcp_routing_table.h"
#include "bcp-config.h"
#include <string.h>
#include "lib/random.h"
#include "fusion.h"
#include "lpm.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif



/*********************************DECLARATIONS*********************************/



static unsigned short fusing_cost;
static unsigned short sensing_cost;
static unsigned short sending_cost;
static unsigned short energy_budget;

static struct routingtable_item_bcp * bestNeighbor;
static int bestWeight;
static bool timerInit;
static bool should_send;
static unsigned short consumed_fusion_packet = 0;
static unsigned short consumed_transfer_packet = 0;

static clock_time_t t_slot_duration = CLOCK_SECOND * SLOT_DURATION;
static struct ctimer time_slot_timer; 

/**
 * \brief      A structure add custom weight estimator metrics to routingtable item 
 *            
 */
struct routingtable_item_bcp {
  struct routingtable_item item;
};


//Memory allocation for the routing table. This is defined here because 
//weight estimators may require to add extra columns to the routingtable_item 
MEMB(routing_table_memb, struct routingtable_item_bcp, MAX_ROUTING_TABLE_SIZE);



void newTimeSlot(struct bcp_conn *c);

/**
 * \return True if the node can send data in this duty cycle. Othersiwse, false.
 */
static bool canSend(){
    //Calculate 
    return should_send;
}



/**
 * \return True if the node can fusion data in this duty cycle. Othersiwse, false.
 */
static bool canFusion(){
    //Calculate 
    return !canSend();
}

static void resetTimer(struct bcp_conn *c){
    if(ctimer_expired(&time_slot_timer)) {
      ctimer_set(&time_slot_timer, t_slot_duration, newTimeSlot, c);
    }
}

/**
 * \return the number of packets can be fused in the current time cycle.
 */
unsigned short get_fusion_budget(){
    
    if(!canFusion())
        return 0;
    
    unsigned short result = (energy_budget/fusing_cost);
 //   PRINTF("DEBUG: Fusion Budget = %d \n", result);
    return (unsigned short)result; 
}


/**
 * \return the number of packets can be transfered in the current time cycle.
 */
unsigned short get_sending_budget(){
    
    if(!canSend())
        return 0;

    unsigned short result = (energy_budget/sending_cost);
    //PRINTF("DEBUG: Sending Budget = %d \n", result);
    return (unsigned short)result; 
}

void set_consumed_sending_budget(unsigned short b){
    if(energy_budget - b*sending_cost < 0)
        energy_budget = 0;
    else
        energy_budget -= (b*sending_cost);
    //printf("consumed sending budget, energy_budget=%d\n",energy_budget);
}

void set_consumed_fusion_budget(unsigned short b){
    if(energy_budget - b*fusing_cost < 0)
        energy_budget = 0;
    else
        energy_budget -= (b*fusing_cost); 
    //printf("consumed fusion budget, energy_budget=%d\n",energy_budget);
}

static void calcSendingCost(){
    //Calculate the energy cost of one transfer
    sending_cost = E_SEND_MIN;
    sending_cost += random_rand() % (E_SEND_MAX - E_SEND_MIN);
    if(sending_cost==0)
        sending_cost = 1;
}

static void performSensing(struct bcp_conn *c){
    //Perform sensing before anything
    uint16_t rx = sensing_rate(&(c->packet_queue));
    printf("Rx=%d\n", rx);
    
    //Sending date based on the current sensing rate
    int i;
    for(i = 0; i < rx; i++){
         uint16_t d = 258;
         packetbuf_copyfrom(&d, 2);
         bcp_send(c);
         
         if(energy_budget-sensing_cost < 1)
             break;
         //Update consumed energy
         energy_budget -= sensing_cost;
    }
}

/**
   * At the beginning every duty cycle this function should be triggered. To find 
   * the best neighbor and to set energy budget for fusion, sensing, and sensing.
   * and save it. 
   */
void newTimeSlot(struct bcp_conn *c){
   
    if(c->isOpen == false)
        return;
    
    timerInit = true;
    PRINTF("DEBUG: Routing table length=%d\n", routingtable_length(&c->routing_table) );
   
    PRINTF("DEBUG: Fusion weight estimator Time slot timer has been triggered \n");
    PRINTF("rimeaddr_node_addr[%d].[%d]\n", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1]);
    
    lpm_set_unusedEnergy(energy_budget);
    
    printf("ACK=%d\n", returnACK());
    resetACK();
    //ASK for solar data
    printf("solar?\n"); //This is required because the data is passed by serial port
    energy_budget = lpm_get_energy_budget();
    //energy_budget = 0;
    calcSendingCost();
    
    if(energy_budget > 1 && !c->isSink){ //LPM is not initialized yet
        int len = bcp_queue_length(&c->packet_queue);
        //printf("len=%d \n", len);

        //Find the best neighbor from the routing table.
        rimeaddr_t* neighborAddr = routingtable_find_routing(&c->routing_table);
        //If there is a neighbor
        if(neighborAddr != NULL){
            bestNeighbor = (struct routingtable_item_bcp *) routing_table_find(&c->routing_table, neighborAddr);

            PRINTF("DEBUG: Best neighbor for this time slot is node[%d].[%d] \n", 
                        bestNeighbor->item.neighbor.u8[0],
                        bestNeighbor->item.neighbor.u8[1]);

            //Calculate the weight for the best neighbor
            int len = (int) bcp_queue_length(&c->packet_queue);
            PRINTF("DEBUG: Queue length for this time slot=%d \n", len);
            int w = len;
            w -= bestNeighbor->item.backpressure;

            bestWeight = w; 
            PRINTF("DEBUG: Best weight for this time slot=%d \n", bestWeight);

            w /= sending_cost; 

            int f =  len;
            f /= fusing_cost;
            PRINTF("DEBUG: w=%d and f=%d \n", w, f);
            int bigerLine = 0;
            if(f <= w){
                should_send = true;
                //printf("send mode\n");
                bigerLine = w;
            }else{
                should_send = false;
                bigerLine = f;
            }
            PRINTF("DEBUG: should_send=%d \n", should_send);
            
            //Set sensing paramters
            sensing_setBigerLine(bigerLine);
            //PRINTF("SENSING_COST=%d\n", sensing_cost);
            sensing_setCost(sensing_cost);
            
            performSensing(c);
            PRINTF("DEBUG: Energy left after sensing=%d\n", energy_budget);
            
            consumed_transfer_packet = 0;
            consumed_fusion_packet = 0;
            //If not send directly
            if(should_send == false && c->isSink == 0){
               performFusion(&c->packet_queue);
            }
            
            PRINTF("DEBUG: Energy left after fusion=%d\n", energy_budget);
            
            //In case of energy left
            if(energy_budget != 0){
                should_send = true;
                PRINTF("DEBUG: Energy budget left (%d) switch to the sending mode\n",energy_budget);
            }
            

        }else{ //If no neighbor
            PRINTF("ERROR: No neighbor for fusion weight estimator to calculate the energy budgets \n");
            //Just performing sensing
            sensing_setBigerLine(1);
            sensing_setCost(sensing_cost);
            performSensing(c);
            
            energy_budget = 0;
        }
   }
    
    if(c->isSink){
        energy_budget = 0;
    }
    //Reset the parameters 
    consumed_transfer_packet = 0;
    consumed_fusion_packet = 0;

    
    //Reset the timer
    resetTimer(c);
    
    timerInit = false;   
}

/*********************************BCP PUBLIC FUNCTION**************************/
int weight_estimator_getWeight(struct bcp_conn *c, struct routingtable_item * it){
    
  
    
    //If it is called by time slot timer
    if(timerInit == true){
        
        //Then calc the weight normally
        struct routingtable_item_bcp * i = (struct routingtable_item_bcp *) it;
        int w = 0;

        
        //Calculate the weight 
        w = (int) bcp_queue_length(&c->packet_queue);
        w -= i->item.backpressure;
        PRINTF("neight_q_log=%d node[%d].[%d] w=%d\n", 
                i->item.backpressure, 
                i->item.neighbor.u8[0],
                i->item.neighbor.u8[1],
                w);
      
        return (int)w; 
    }else{
        //If it is called by the routing table, then only to send the best neighbor 
        //which is calculated at the beginning each time cycle.
        if(it == bestNeighbor){
            return bestWeight;
        }else{
            return 1;
        }
    }
}

void weight_estimator_sent(struct routingtable_item * it, 
                                struct bcp_queue_item *qi, 
                                uint16_t attempts){
}

void weight_estimator_init(struct bcp_conn *c){
    PRINTF("DEBUG: Fusion weight_estimator_init has been called \n");
    c->routing_table.memb = &routing_table_memb;
    memb_init(&routing_table_memb);
    
    //Calculate The energy cost of one fuse packet
    fusing_cost = E_FUSE_MIN;
    fusing_cost += random_rand() % (E_FUSE_MAX - E_FUSE_MIN);
    
    if(fusing_cost==0)
        fusing_cost = 1;
    //Calculate the energy cost of one transfer
    calcSendingCost();
    
    //Calculate the energy cost for one sensing 
    sensing_cost = E_SENSING_MIN;
    sensing_cost += random_rand() % (E_SENSING_MAX - E_SENSING_MIN);
    if(sensing_cost==0)
        sensing_cost = 1;
    
    
    PRINTF("DEBUG: For this node: fusing_cost=%d, sending_cost=%d, and sensing_cost=%d \n", 
                fusing_cost,
                sending_cost,
                sensing_cost);
    
    PRINTF("DEBUG: Routing table length=%d\n", routingtable_length(&c->routing_table) );
   
    //Start Time Slot Timer
    resetTimer(c);
}

void weight_estimator_record_init(struct routingtable_item * it){
    
}

void weight_estimator_print_item(struct bcp_conn *c, struct routingtable_item *item){
    struct routingtable_item_bcp * i = item; 
    PRINTF("weight: %d\n", weight_estimator_getWeight(c, item));
}