/**
 * \file
 *         The default implementation of backpressure routing (see \ref bcp.h).
 */
#include "bcp.h"

#include "net/rime.h"
#include "net/rime/unicast.h"
#include "net/rime/broadcast.h"
#include "net/netstack.h"
#include "bcp_extend.h"
#include "bcp_queue_allocator.h"
#include "hop_counter.h"

#include <stddef.h>  //For offsetof
#include "lib/list.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


/*********************************DECLARATIONS*********************************/
static const struct packetbuf_attrlist attributes[] = {
    BCP_ATTRIBUTES
    PACKETBUF_ATTR_LAST };

/**
 * \brief      A structure for beacon messages.
 *
 *             An opened BCP connection broadcast beacon messages on regular 
 *             basis to the one-hop neighbors.            
 */
struct beacon_msg {
  /**
  * The queue length of the node. 
  */
  uint16_t queuelog; 
};

/**
 * \brief      A structure for beacon request messages.
 *
 *             This is type of message is used when the node wants to ask its 
 *             neighbor to send their beacon messages. A beacon request message is 
 *             usually broadcasted when a node failed to delivery a data packet 
 *             to its best neighbor.
 */
struct beacon_request_msg {
  /**
  * The queue length of the node. 
  */
  uint16_t queuelog;
};

/**
 * \brief      A structure for acknowledgment messages.
 */
struct ack_msg {
};



static void send_beacon_request(void *ptr);
static void send_beacon(void *ptr);
static bool isBeacon();
static void prepare_packetbuf();
static bool isBeaconRequest();
static bool isBroadcast(rimeaddr_t * addr);
static void send_packet(void *ptr);
struct bcp_queue_item* push_packet_to_queue(struct bcp_conn *c);
static void send_ack(struct bcp_conn *bc, const rimeaddr_t *to);
static void retransmit_callback(void *ptr);
static void setBusy(struct bcp_conn *bcp_conn, bool , char * sourceName);
static int ackCoounter = 0;


int returnACK(){
   return ackCoounter; 
}

void resetACK(){
    ackCoounter = 0;
}

/*********************************CALLBACKS************************************/
/**
 * \breif Called when an ACK message is recieved
 * \param c The unicast connection 
 * \param from the address of the sender
 *      
 *      In BCP, the unicast channel is used to send ACK messages. This function 
 *      is the callback function for the opened unicast channel.
 */
static void recv_from_unicast(struct unicast_conn *c, const rimeaddr_t *from)
{
    struct bcp_queue_item *i;
    struct ack_msg m;
    struct routingtable_item * ri;

    PRINTF("DEBUG: Receiving an ACK via the unicast channel\n");
    
    
    // Cast the unicast connection as a BCP connection
    struct bcp_conn *bcp_conn = (struct bcp_conn *)((char *)c
        - offsetof(struct bcp_conn, unicast_conn));
    
    setBusy(bcp_conn, true, "recv_from_unicast");
    
    //Copy the header
    memcpy(&m, packetbuf_dataptr(), sizeof(struct ack_msg));
    
    //Remove the packet from the packet queue
    i = bcp_queue_top(&bcp_conn->packet_queue);
    
    if(i != NULL) {
      
        PRINTF("DEBUG: ACK received removing the current active packet from the queue\n");
        // Reset BCP connection for next packet to send
        bcp_conn->tx_attempts = 0;
        
        
        //Notify user that this packet has been sent
        if(bcp_conn->cb->sent != NULL){
            prepare_packetbuf();
            packetbuf_copyfrom(i->data, i->hdr.packet_length);
            bcp_conn->cb->sent(bcp_conn);        
        }
        
        //Remove the packet from the queue
        bcp_queue_remove(&bcp_conn->packet_queue, i);
        
        struct routingtable_item* neigh = routing_table_find(&bcp_conn->routing_table, from);
        if(neigh != NULL && neigh->backpressure > 5)
         neigh->backpressure -= 5; //Increase neighbor weight if the ACK not received 
        
        ackCoounter++;
        
        // // Reset the send data timer in case their are other packets in the queue
       // retransmit_callback(bcp_conn);
        
    }else{
        PRINTF("ERROR: Cannot find the current active packet. ACK cannot be sent\n");
    }
    
    setBusy(bcp_conn, false, "recv_from_unicast");
}

/**
 * \breif Called whenever a new packet has been received by the broadcast channel
 * \param c Broadcast channel
 * \param from The sender rime address
 * 
 *      In BCP, the broadcast channel is used to send two types of packets (user
 *      data packets and beacons). This function is the callback function for the
 *      opened broadcast channel.
 */
static void recv_from_broadcast(struct broadcast_conn *c,
                                const rimeaddr_t *from)
{
    //Convert c to bcp instance
    struct bcp_conn *bc = (struct bcp_conn *)((char *)c
      - offsetof(struct bcp_conn, broadcast_conn));
   
    //Find the destination address of the packet 
    rimeaddr_t destinationAddress;
    rimeaddr_copy(&destinationAddress, packetbuf_addr(PACKETBUF_ADDR_ERECEIVER));
    
     setBusy(bc, true, "recv_from_broadcast");
    
    //If it is a broadcast
    if(isBroadcast(&destinationAddress)){
        //It is either beacon or beacon request. 
        if(isBeacon()){
           
    
            //Construct the beacon message
            struct beacon_msg beacon;
            memcpy(&beacon, packetbuf_dataptr(), sizeof(struct beacon_msg));
             PRINTF("DEBUG: Receiving a beacon from node[%d].[%d] and new queuelength=%d\n", 
                     from->u8[0], 
                     from->u8[1],
                     beacon.queuelog);
   
            //Update the queue for that neighbor
            routing_table_update_queuelog(&bc->routing_table, from, beacon.queuelog, 0);
          
        }
        
    }else //If this node is the destination 
        if(rimeaddr_cmp(&destinationAddress, &rimeaddr_node_addr)){
            
            //Abstract the message
            struct bcp_queue_item * dm = (struct bcp_queue_item *) packetbuf_dataptr();
            
            if(bc->ce != NULL && bc->ce->onReceivingData != NULL)
                        bc->ce->onReceivingData(bc, dm);
                    
            PRINTF("DEBUG: Received a forwarded data packet sent to node[%d].[%d] (Origin: [%d][%d]), BCP=%d, delay=%x",
                  destinationAddress.u8[0], 
                  destinationAddress.u8[1], 
                  dm->hdr.origin.u8[0],
                  dm->hdr.origin.u8[1],
                  dm->hdr.bcp_backpressure,
                  dm->hdr.delay);
            //MSP430 Limitations 
            PRINTF(", len=%d\n", 
                  dm->hdr.packet_length);
            PRINTF(", len=%d\n", 
                  dm->hdr.packet_length);
            
            if(!bc->isSink){
                //Add this packet to the queue so that we can forward it in the near future
                struct bcp_queue_item* itm;
                itm = bcp_queue_push(&bc->packet_queue, dm);
                 //Notify the extender
               
                if(itm != NULL){
                     itm->hdr.lastProcessTime = clock_time();
                    
                     
                      //Update the routing table
                      routing_table_update_queuelog(&bc->routing_table, from, dm->hdr.bcp_backpressure, 1);
               
                      //Send ACK
                      send_ack(bc, from);
               
                     
                }else{
                    PRINTF("ERROR: Packet Queue is full. ACK will not be sent to node[%d].[%d]\n", 
                            from->u8[0], from->u8[1]);
                }
                
               
               
                
             }else{
                PRINTF("Before saving the message\n");
                
               //Save the message
               char pk[dm->hdr.packet_length];
               struct bcp_queue_item* bcp_pk = &pk;
               memcpy(&pk, dm, dm->hdr.packet_length);
               
               PRINTF("After saving the message\n");
                //If it is Sink
               PRINTF("DEBUG: Sink Received a new data packet from node[%d].[%d], user will be notified, total delay(ms)=%x\n", 
                       bcp_pk->hdr.origin.u8[0], 
                       bcp_pk->hdr.origin.u8[1],
                       dm->hdr.delay);
               printf("delay=%ld\n", dm->hdr.delay);
               //Send ACK
               send_ack(bc, from);

               //Notify end user callbacks
               //We need to rebuild packetbuf since we called send_ack
               prepare_packetbuf();
               
               memcpy(packetbuf_dataptr(), &bcp_pk->data, MAX_USER_PACKET_SIZE);
                        
               //Notify user callback
               if(bc->cb->recv != NULL)
                  bc->cb->recv(bc, &bcp_pk->hdr.origin);
               else 
                  PRINTF("ERROR: BCP cannot notify user as the receive callback function is not set.\n");
               
               //Update the routing table
               routing_table_update_queuelog(&bc->routing_table, from, bcp_pk->hdr.bcp_backpressure, 0);
            }

           
            
    }else{
        //When the node is not the destination for the data pack. Just abstract 
        //the queue log from the packet
         struct bcp_queue_item * dm = (struct bcp_queue_item *) packetbuf_dataptr();
            PRINTF("DEBUG: Received a forwarded data packet sent to node[%d].[%d] (Origin: [%d][%d]), BCP=%d, delay=%x \n",
                  destinationAddress.u8[0], 
                  destinationAddress.u8[1], 
                  dm->hdr.origin.u8[0],
                  dm->hdr.origin.u8[1],
                  dm->hdr.bcp_backpressure,
                  dm->hdr.delay);
            
      
        
        routing_table_update_queuelog(&bc->routing_table, from, dm->hdr.bcp_backpressure, 0);
    }
     
     setBusy(bc, false, "recv_from_broadcast");
    
}


/**
 * \brief Called when a data packet or beacon has been sent via the broadcast channel
 * \param c A pointer to the broadcast channel
 * \param status the result of sending
 * \param transmissions number of attempts 
 * 
 *      In BCP, the broadcast channel is used to send two types of packets (user
 *      data packets and beacons). This function is the callback function for the
 *      opened broadcast channel.
 */
static void sent_from_broadcast(struct broadcast_conn *c, int status,
                                int transmissions)
{
        
     // Cast the broadcast connection as a BCP connection
    struct bcp_conn *bcp_conn = (struct bcp_conn *)((char *)c
      - offsetof(struct bcp_conn, broadcast_conn));

    // If it is a beacon
    if(isBeacon()) {
      setBusy(bcp_conn, false, "sent_from_broadcast#beacon");

      
    }else if(isBeaconRequest()){
         setBusy(bcp_conn, false, "sent_from_broadcast#beaconRequest");
         
    }else{
       setBusy(c, false, "sent_from_broadcast#data");
    
    }
}

/**
 * Defines the callback fuctions for the broadcast and unicast channels
 */
static const struct broadcast_callbacks broadcast_callbacks = {
    recv_from_broadcast,
    sent_from_broadcast };
static const struct unicast_callbacks unicast_callbacks = { recv_from_unicast };
/******************************************************************************/

/*********************************UTILITIES************************************/
/**
 * 
 * \param addr
 * \return true if the given address is the general broadcast address. Otherwise, false
 */
static bool isBroadcast(rimeaddr_t * addr){
    rimeaddr_t broadcastAddress;
    broadcastAddress.u8[0] = 0;
    broadcastAddress.u8[1] = 0;
    return (rimeaddr_cmp(&broadcastAddress, addr));
}



/**
 * \return true if the current packet in packetbuf is beacon(see \ref "struct beacon_msg"). Otherwise, false.
 */
static bool isBeacon(){
    return (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE) 
            == PACKETBUF_ATTR_PACKET_TYPE_BEACON);
}

/**
 * \return true if the current packet in packetbuf is beacon_request(see \ref "struct beacon_request_msg"). Otherwise, false.
 */
static bool isBeaconRequest(){
    return (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE) 
            == PACKETBUF_ATTR_PACKET_TYPE_BEACON_REQUEST);
}


/**
 * \breif Broadcasts a beacon request message(see \ref "struct beacon_request_msg") to the one-hop neighbors.
 * \param ptr the bcp connection
 * 
 */
static void send_beacon_request(void * ptr){
    struct bcp_conn *c = ptr; 
    struct beacon_request_msg * br_msg;
    
    if(c->busy == false)
        setBusy(c, true, "send_beacon_request");
    else{
        if(c->isSink == true){
                // Reset the beacon timer
          if(ctimer_expired(&c->beacon_timer)) {
            clock_time_t time = BEACON_TIME * 5;
            ctimer_set(&c->beacon_timer, time, send_beacon, c);
          }
        }
        return;
    }
    
    //Delete all the records in the routing table
    //routingtable_clearForwardable(&c->routing_table);
    
    prepare_packetbuf();
    packetbuf_set_datalen(sizeof(struct beacon_request_msg));
    
    br_msg = packetbuf_dataptr();
    memset(br_msg, 0, sizeof(br_msg));
    
    // Store the local backpressure level to the backpressure field
    br_msg->queuelog = bcp_queue_length(&c->packet_queue);
     
    //Update the packet buffer 
    //TDOO: Check if this is required
    memcpy(packetbuf_dataptr(), br_msg, sizeof(struct beacon_request_msg));
    
    // Set the packet type using packetbuf attribute
    packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                     PACKETBUF_ATTR_PACKET_TYPE_BEACON_REQUEST);

    PRINTF("DEBUG: Beacon Request sent via the broadcast channel. BCP=%d\n",  br_msg->queuelog);
    
    // Broadcast the beacon
    broadcast_send(&c->broadcast_conn);
    
    // Reset the beacon timer
    if(ctimer_expired(&c->beacon_timer)) {
        clock_time_t time = BEACON_TIME;
        ctimer_set(&c->beacon_timer, time, send_beacon, c);
    }
}

/**
 * \breif Broadcasts a beacon to the one-hop neighbors 
 * \param ptr The opened BCP connection
 * 
 *      Sends a beacon via the opened broadcast channel. This function is usually
 *      called by the beacon_timer to send a beacon message when the broadcast 
 *      channel is free. 
 * 
 */
static void send_beacon(void *ptr)
{
  struct bcp_conn *c = ptr; 
  struct beacon_msg *beacon;

  PRINTF("DEBUG: Send Beacon timer has been triggered. c->busy=%d\n",c->busy);
   
  //Check if the channel is free 
  if(c->busy == false)
    setBusy(c, true, "send_beacon");
  else
    return;

  //Prepare the packet for the beacon 
  prepare_packetbuf();
  packetbuf_set_datalen(sizeof(struct beacon_msg));
  beacon = packetbuf_dataptr();
  memset(beacon, 0, sizeof(beacon));
   
  // Store the local backpressure level to the backpressure field
  beacon->queuelog = bcp_queue_length(&c->packet_queue); 

  //Update the packet buffer
  //TDOO: Check if this is required
  memcpy(packetbuf_dataptr(), beacon, sizeof(struct beacon_msg));

  // Set the packet type using packetbuf attribute
  packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                     PACKETBUF_ATTR_PACKET_TYPE_BEACON);
   
  PRINTF("DEBUG: Sending a beacon via the broadcast channel. BCP=%d\n",  beacon->queuelog);
    
  // Broadcast the beacon
  broadcast_send(&c->broadcast_conn);
}

/**
 * \breif Adds the current packetbuf to the packet queue for the given bcp connection.
 * \param c the bcp connection
 * \return The queue item for the packet. If the packet cannot be added to the queue, this function will return NULL.
 */
 struct bcp_queue_item* push_packet_to_queue(struct bcp_conn *c){

  
     struct bcp_queue_item newRow;
    
    //Packetbuf should not be empty
    if(packetbuf_dataptr() == NULL){
        PRINTF("ERROR: Packetbuf is empty; data cannot be added to the queue\n");
        return NULL;
    }
    
    //Sets the fields of the new record
    newRow.next = NULL;
    newRow.hdr.bcp_backpressure = 0;
    newRow.hdr.packet_length = sizeof(struct bcp_queue_item);
    memcpy(newRow.data, packetbuf_dataptr(), MAX_USER_PACKET_SIZE);
    
    
    
    struct bcp_queue_item * result = bcp_queue_push(&c->packet_queue, &newRow);
    
    if(c->ce != NULL && c->ce->onUserSendRequest != NULL && result != NULL)
                c->ce->onUserSendRequest(c, result);
    
    return result;
    
    
}
 
 
/**
 * \breif Called by the retransmission timer
 * \param ptr the bcp connection
 * 
 *     Every BCP connection has a retransmission timer. retransmission timer is 
 *     used to send a beacon request message when no ACK message is received. 
 */
static void retransmit_callback(void *ptr)
{
    struct bcp_conn *c = ptr;
    PRINTF("DEBUG: Attempt to retransmit the data packet\n");
   
    //Reschedule the send timer.
    if(!c->isSink && ctimer_expired(&c->send_timer)) {
        clock_time_t time = RETX_TIME * (c->tx_attempts+1);
        ctimer_set(&c->send_timer, time, send_packet, c); 
    }

}


 /**
  * \breif Sends user data packet via the broadcast channel for the given bcp connection.
  * \param ptr the bcp connection.
  * 
  *     This function sends the top packet in the packet queue of the given bcp 
  *     connection. It is usually called by the 'send' timer. Each opened bcp 
  *     channel has a timer to send the packets existing in the packet queue. 
  *     
  */
 static void send_packet(void *ptr)
{
    struct bcp_conn *c = ptr;
    struct bcp_queue_item * i;
    
    PRINTF("DEBUG: Send packet timer has been triggered. c->busy=%d\n", c->busy);
    
    // If it is busy, just return and wait for the second opportunity
    if(c->busy == true){
        PRINTF("DEBUG: BCP is currently busy. Resend the data packets later\n");
        
        //Reschedule the send timer. 
        retransmit_callback(c);
        return;
    }
    
    //Preparing bcp to send a new message
    setBusy(c, true, "send_packet");
    
    i = bcp_queue_top(&c->packet_queue);
   
    //Find the best neighbor to send
    rimeaddr_t* neighborAddr = routingtable_find_routing(&c->routing_table);
    
 
    if( i == NULL || neighborAddr == NULL){
         if(neighborAddr == NULL)
             PRINTF("DEBUG: No neighbor has been found; data cannot be sent via the BCP\n");
        else
             PRINTF("DEBUG: Packet queue is empty; start beaconing \n");
        
        setBusy(c, false, "send_packet");
        // Start beaconing
        if(ctimer_expired(&c->beacon_timer)){
            clock_time_t time = BEACON_TIME;
            ctimer_set(&c->beacon_timer, time, send_beacon, c);
        }
        
        // Resend the send data timer
        retransmit_callback(c);
        return;
    }
     
    // Stop beaconing
    if(!ctimer_expired(&c->beacon_timer)) {
        ctimer_stop(&c->beacon_timer);
    }
   if(c->ce != NULL && c->ce->prepareSendingData != NULL)
    c->ce->prepareSendingData(c, i);
    //Clear the header of the packet
    prepare_packetbuf();
    // Set the packet type as data
    packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                     PACKETBUF_ATTR_PACKET_TYPE_DATA);
    //Update the header
    packetbuf_set_addr(PACKETBUF_ADDR_ERECEIVER, neighborAddr); //Set the destination address

    //Add backpressure meta data to the header. All these meta data can be overwritten by the extender
    i->hdr.bcp_backpressure = bcp_queue_length(&c->packet_queue); 
    i->hdr.delay = i->hdr.delay + clock_time() - i->hdr.lastProcessTime;
    i->hdr.lastProcessTime = i->hdr.lastProcessTime;

    //Notify the extender
    if(c->ce != NULL && c->ce->beforeSendingData != NULL){
             struct bcp_queue_item * checkItm = c->ce->beforeSendingData(c, i);
             if(checkItm == NULL){
                  PRINTF("DEBUG: Aborting sending packet based on the extender result \n");
                  setBusy(c, false, "send_packet");
                  
                  if(ctimer_expired(&c->beacon_timer)){
                    clock_time_t time = BEACON_TIME;
                    ctimer_set(&c->beacon_timer, time, send_beacon, c);
                  }
                  
                  retransmit_callback(c);
                  return;
             }else{
                 i = checkItm;
             }
    }

    //Copy the data to the packetbuf
    packetbuf_set_datalen(i->hdr.packet_length);
    memcpy(packetbuf_dataptr(),i, i->hdr.packet_length);

     //Remove pointers
    struct bcp_queue_item* pI = packetbuf_dataptr();
    pI->next = NULL;

    c->tx_attempts += 1;
    
    struct routingtable_item* neigh = routing_table_find(&c->routing_table, neighborAddr);
    neigh->backpressure += 5; //Decrease neighbor weight if the ACK not received 

    PRINTF("DEBUG: Sending a data packet to node[%d].[%d] (Origin: [%d][%d]), BC=%d,len=%d, data[0]=%x \n", 
            neighborAddr->u8[0], 
            neighborAddr->u8[1],
            pI->hdr.origin.u8[0],
            pI->hdr.origin.u8[1],
            pI->hdr.bcp_backpressure,
            pI->hdr.packet_length,
            pI->data[0]);

     
     //Send the data packet via the broadcast channel
    broadcast_send(&c->broadcast_conn);

    //Notify the extender
    if(c->ce != NULL && c->ce->afterSendingData != NULL)
                    c->ce->afterSendingData(c, i);

    
    // Resend the send data timer
    retransmit_callback(c);
   
    
}
 /**
  * Sends an ACK to the given neighbor.
  * @param bc the BCP connection.
  * @param to the rime address of the neighbor
  */
 static void send_ack(struct bcp_conn *bc, const rimeaddr_t *to){
    
     struct ack_msg *ack;

     prepare_packetbuf();
     packetbuf_set_datalen(sizeof(struct ack_msg));
     ack = packetbuf_dataptr();
     memset(ack, 0, sizeof(struct ack_msg));
     packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                       PACKETBUF_ATTR_PACKET_TYPE_ACK);
     //We use a unicast channel to send ACKS
     unicast_send(&bc->unicast_conn, to);
 }
 
 /**
  * \breif Stops all the timers of the given BCP connection.
  * \param c an opened BCP connection
  */
 static void stopTimers(struct bcp_conn *c){
     ctimer_stop(&c->send_timer);
     ctimer_stop(&c->beacon_timer);
     ctimer_stop(&c->retransmission_timer);
     ctimer_stop(&c->check_timer); 
 }
 
 /**
  * Prepares the packetbuf of the node for a new packet.
  */
 static void prepare_packetbuf(){
     //PRINTF("DEBUG: Prepare Packetbuf\n");
     memset(packetbuf_dataptr(), '\0', packetbuf_datalen()+1);
     packetbuf_clear();
     
 }
 
 /**
  * \breif Notifies users that the current packet in packetbuf is dropped from bcp
  */
 static void packet_dropped(struct bcp_conn *c){
        //Notify user that this packet has been sent
        if(c->cb->dropped != NULL){
            c->cb->dropped(c);        
        }
 }
 
 static void setBusy(struct bcp_conn *bcp_conn, bool isBusy, char * sourceName){
     
     bcp_conn->busy = isBusy;
     PRINTF("DEBUG: Changing BCP busy flag to %d. Called from %s\n", isBusy, sourceName);
}
 
 /**
 * Triggered by the check timer to check the current condition of BCP. 
 */
 static void check_bcp(void *ptr){
    struct bcp_conn *c = ptr; 
    printf("Check timer has been trigger \n");
    
/*
    //Make sure the send_timer is still alive. Otherwise, start it
    if(!c->isSink && ctimer_expired(&c->send_timer)) {
      clock_time_t time = SEND_TIME_DELAY;
      ctimer_set(&c->send_timer, time, send_packet, c);
    }
*/

    //For Sink, check the beacon timer
    if(c->isSink && ctimer_expired(&c->beacon_timer)){
            clock_time_t time = BEACON_TIME;
            ctimer_set(&c->beacon_timer, time, send_beacon, c);
     }

    //Reset the check timer again - infinite loop 
    if(ctimer_expired(&c->check_timer)) {
      clock_time_t timeCheck = CLOCK_SECOND * SLOT_DURATION * 10;
      ctimer_set(&c->check_timer, timeCheck, check_bcp, c);
    }
 }

/******************************************************************************/

/*********************************BCP PUBLIC FUNCTION**************************/
void bcp_open(struct bcp_conn *c, uint16_t channel,
              const struct bcp_callbacks *callbacks)
{
    PRINTF("DEBUG: Opening a bcp connection\n");
    //Set the end user callback function
    c->cb = callbacks;
    //Set the default extender interface 
    c->ce = NULL;
    
    // Initialize the lists containing in the BCP object
    LIST_STRUCT_INIT(c, packet_queue_list);
    LIST_STRUCT_INIT(c, routing_table_list);
    c->isOpen = true;
    
    //Initialize nested components
    routing_table_init(c);
    weight_estimator_init(c);
    bcp_queue_init(c);
    hop_counter_init(c);   
    //Ask queue allocator to allocate memeory for the queue
    bcp_queue_allocator_init(c);
   
    PRINTF("DEBUG: Open a broadcast connection for the data packets and beacons of the BCP\n");
    broadcast_open(&c->broadcast_conn, channel, &broadcast_callbacks);
    channel_set_attributes(channel, attributes);
    
    PRINTF("DEBUG: Open the unicast connection for BCP's ACKs\n");
    unicast_open(&c->unicast_conn, channel + 1, &unicast_callbacks);
    channel_set_attributes(channel + 1, attributes);
   
    //Broadcast the first beacon
    send_beacon(c);
   
    // Reset the send data timer
    
    //Make sure the send_timer is still alive. Otherwise, start it
    if(!c->isSink && ctimer_expired(&c->send_timer)) {
      clock_time_t time = SEND_TIME_DELAY;
      ctimer_set(&c->send_timer, time, send_packet, c);
    }
     
    
    if(ctimer_expired(&c->beacon_timer)) {
      clock_time_t timeCheck = CLOCK_SECOND;// * SLOT_DURATION * 5;
      ctimer_set(&c->check_timer, timeCheck, check_bcp, c);
    }
    
    
}

void bcp_close(struct bcp_conn *c){
  // Close the broadcast connection
  broadcast_close(&c->broadcast_conn);

  // Close the unicast connection
  unicast_close(&c->unicast_conn);
  
  //Clear both routing table and packet queue
  routingtable_clear(&c->routing_table);
  bcp_queue_clear(&c->packet_queue);
  
  //Stop the timers
  stopTimers(c);
  
  c->isOpen = false;
 
}

int bcp_send(struct bcp_conn *c){
    struct bcp_queue_item *qi;
    int result = 0;
    int maxSize = MAX_USER_PACKET_SIZE;
    
     setBusy(c, true, "bcp_send");
    //Check the length of the packet
    if(packetbuf_datalen()> maxSize){
        PRINTF("ERROR: Packet cannot be sent. Data length is bigger than maximum packet size\n");
        packet_dropped(c);
        return 0;
    }
    
    qi = push_packet_to_queue(c);
    PRINTF("DEBUG: Receiving user request to send a data packet \n");
    
    if(qi != NULL){
        // Set the origin of the packet
        rimeaddr_copy(&(qi->hdr.origin), &rimeaddr_node_addr);
        qi->hdr.delay = 0;
        qi->hdr.lastProcessTime = clock_time();
        // We have data to send, stop beaconing
        
        result = 1;
    }else{
        packet_dropped(c);
    }
    
    setBusy(c, false, "bcp_send");


    return result;
}

void bcp_set_sink(struct bcp_conn *c, bool isSink){
    
    c->isSink = isSink;
    
    if(c->isSink == true){
        PRINTF("DEBUG: This node is set as a sink \n");
        // Start beaconing
        if(ctimer_expired(&c->beacon_timer)){
            clock_time_t time = BEACON_TIME;
            ctimer_set(&c->beacon_timer, time, send_beacon, c);
        }
    }
   
}



