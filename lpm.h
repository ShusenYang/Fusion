
/* 
 * File:   lpm.h
 *
 * Created on December 17, 2013, 12:57 PM
 */

#ifndef LPM_H
#define	LPM_H

/**
 * Sets the input energy coming from the solar panel
 */
void lpm_set_input(uint16_t energy);
/**
 * @return energy budget for the current time slot
 */
uint16_t lpm_get_energy_budget();

/**
 * @return the current battery level
 */
uint32_t lpm_get_battery_level();

/**
 * Sets the energy budget left(unused) during the previous time cycle.
 */
void lpm_set_unusedEnergy(uint16_t energy);
#endif	/* LPM_H */

