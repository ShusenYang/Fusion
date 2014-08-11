/**
 * \file
 *         Header file for sensing rate controller. The objective of this controller
 *         is to control the number of packets a node can generate at every time slot.
 * 
 * 
 *      
 */


#ifndef SENSING_CONTROL_H
#define	SENSING_CONTROL_H


/**
 * \brief  Finds appropriated sensing rate for the given queue
 * \return the sensing rate for the current time slot
 *         
 *      This function calculates the sensing rate based on the current condition of 
 *      the given queue. The returned value is always positive and integer. This
 *      function should be called at the beginning of every time slot (t) to 
 *      determined the maximum number of packets that the node can generate 
 *      during slot (t).  
 *            
 */
uint16_t sensing_rate(struct bcp_queue *s);


/**
 * Sets the bigerLine parameter
 */
void sensing_setBigerLine(int p);

/**
 * Sets the energy cost for one sensing operations
 */
void sensing_setCost(int cost);

#endif	/* SENSING_CONTROL_H */

