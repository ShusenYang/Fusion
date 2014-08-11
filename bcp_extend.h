/**
 * \file
 *      This interface can be extended by any component wishing to extend the 
 *      functionality of BCP implementation.
 *    
 *      
 */
#ifndef BCP_EXTENDER_H
#define	BCP_EXTENDER_H



/** 
 *  An interface which can be attached to an opened BCP connection in order to 
 *  extends the functionality of BCP. All the callbacks functions are called by 
 *  the BCP when corresponding events occur. 
 */
struct bcp_extender{ 
  
 /**
   * Called by BCP when BCP prepares a new data packet before sending it. This 
   * function is called before 'beforeSendingData' event. 
   */
  void (*prepareSendingData)(struct bcp_conn *c,  struct bcp_queue_item* itm);
    
  
  /**
   * Called by BCP before broadcasting a data packet.
   * 
   * \return the modified bcp_queue_item. If this function returns NULL, BCP will abort sending this packet.
   */
  struct bcp_queue_item* (*beforeSendingData)(struct bcp_conn *c,  struct bcp_queue_item* itm);
  /**
   * Called by BCP after broadcasting a data packet
   */
  void (*afterSendingData)(struct bcp_conn *c,  struct bcp_queue_item* itm);
  /**
   * Called by BCP after successfully receiving a new data packet
   */
  void (*onReceivingData)(struct bcp_conn *c, struct bcp_queue_item* itm);
  
  /**
   * Called by BCP when a user requests BCP to send a new data packet (i.e. calling fusion 'bcp_send').
   */
  void (*onUserSendRequest)(struct bcp_conn *c, struct bcp_queue_item* itm);
};

#endif	/* BCP_EXTENDER_H */

