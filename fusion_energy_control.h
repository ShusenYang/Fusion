/* 
 * File:   fusion_energy_control.h
 *
 * Created on February 6, 2014, 2:32 PM
 */

#ifndef FUSION_ENERGY_CONTROL_H
#define	FUSION_ENERGY_CONTROL_H

unsigned short get_sending_budget();
unsigned short get_fusion_budget();

void set_consumed_sending_budget(unsigned short b);
void set_consumed_fusion_budget(unsigned short b);

#endif	/* FUSION_ENERGY_CONTROL_H */

