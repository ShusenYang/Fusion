/**
 * \file
 *         Default implementation of local power management component. This implementation
 *         is based on PHD-27 specification.
 */

#include "contiki.h"
#include "lpm.h"
#include "fusion_config.h"
#include <stdio.h>

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


static uint32_t batteryMax = BATTERY_MAX;
static uint32_t batteryLevel = BATTERY_MAX; //Runtime battery level - this will be updated at every slot
static uint16_t batteryLeakage = 0; //Battery leakage if any
static int32_t energyConsumption; //Energy Budget

static int32_t maxConsumption = 125; //MAX energy budget drown from the battery
static float minConsumption = 50; //Min energy requires for single task execute

static float rechargingEfficiency = 0.74;


//Flags
static char NightStarted = 1; //Indicates whether the night has been started or not.
static char initialized = 0; //Indicates whether the LPM component has been initialized or not



static int32_t phi;
static int32_t extraPhi = 1000000;
static uint16_t dayTimeFirstSlot = 1; //i_0(d)
static uint16_t nightFirstTimeSlot; //i_1(d)
static uint16_t previousDayTimeSlot = 0xffff; //i_0(d+1)
static uint16_t dayDuration = 0; //M
static uint16_t slotCounter = 1; //i
static char previousSlotType; //Stores whether the previous slot was in daytime or night
static uint32_t Eno ; //ENO calculated in the beginning of every day - i_0
static uint16_t preSolar; //Holds the solar energy input for the current time slot


/**
 * Checks whether energy consumption is more than maxConsumption or not. 
 * @return if energy is more than maxConsumption, return maxConsumption. Otherwise,
 * this method returns the given energy
 */
static int32_t checkConsumption(int32_t energy){
    int32_t result = energy;
    
    //Check fir the min level
    if( (int32_t) result < (int32_t) minConsumption){
        result =  minConsumption;
        PRINTF("ERROR:  Energy consumption is lower than the allowed amount \n");
    }
    
    //Check for the max level
    if((int32_t) result > (int32_t)maxConsumption){
        result = maxConsumption;
        PRINTF("ERROR: Energy consumption is larger than the allowed amount \n");
    }
     return result;
}

/**
 * Checks the battery level. Battery level should be between >= 0 and batteryMax 
 */
static void checkBatteryLevel(){
     //batteryLevel limits
    if(batteryLevel <= 0){
        PRINTF("ERROR: Battery level is zero \n");
        batteryLevel = 0;
    }
    
    
    if(batteryLevel > (uint32_t) batteryMax ){
        PRINTF("ERROR: Battery level is above the allowed limit \n");
        batteryLevel = batteryMax;
    }
}

/**
 * Setups slotCounter 
 */
static void newCycle(){ 
    slotCounter++;
    
    //Slot Counter should never be larger than M
    if(slotCounter > dayDuration && dayDuration != 0){
       //PRINTF("ERROR: SlotCounter(%d) becomes larger than M(%d)\n",slotCounter, dayDuration );
       dayDuration = slotCounter;
    }
    
   
}

static int changingCounter;
static char prevIsDayResult = 0;

/**
 * @return 1 if this slot is in a daytime. Otherwise, 0.
 */
static char isDayTime(uint16_t solar_energy){
    
    char result;
    
    if(solar_energy > 1){ //Zero because we want to De-coupling Emin from Phi  
        result = 1;
    
    }else{
        result = 0;
    }
    
    if(result != prevIsDayResult && changingCounter++< 20)
        return prevIsDayResult;
    else{
        prevIsDayResult = result;
        changingCounter = 0;
    }
    
    return result;
}
 
int nightCounter = 0;
/**
 * Calculates ENO if required
 */
static void slotUpdate(uint16_t solar_energy){
    
    char currentSlotType = isDayTime(solar_energy);

    if(currentSlotType != previousSlotType){
        //If current slot day but the last slot is night, start a new day
        if( currentSlotType == 1) 
        {          
            if(NightStarted == 1){
                if(initialized == 1 )
                        previousDayTimeSlot = slotCounter;
                dayTimeFirstSlot = 1;
                slotCounter = 1;
                NightStarted = 0;
            }        
        }else{
           //If the solar energy is zero, set night started flag
            if(NightStarted == 0 && solar_energy == 0){
                initialized = 1;            
                NightStarted = 1;
                
            }else
                return;
            
            nightFirstTimeSlot = slotCounter;
            
            if(previousDayTimeSlot == 0xffff)
                previousDayTimeSlot = 287;//nightFirstTimeSlot * 2; 
            
            //previousDayTimeSlot = nightFirstTimeSlot * 2;
            
            //Update M
            dayDuration = previousDayTimeSlot - dayTimeFirstSlot;

            //Update ENO
            Eno =  (minConsumption + batteryLeakage) 
                * (dayTimeFirstSlot + dayDuration -  nightFirstTimeSlot);

           PRINTF("DEBUG: i_1(d)=%d, M=%d, i_0(d+1)=%d\n", 
                    nightFirstTimeSlot,
                    dayDuration,
                    previousDayTimeSlot
                  );
        }  
    }
    
  
   previousSlotType = currentSlotType;
}

/**
 * Calculates Phi using Shusan's JSAC paper.
 */
static void calcPhi(uint16_t solar_energy){

    
    //Call for ENO
    slotUpdate(solar_energy);   

    //the first day is ingored for initialization purposes 
    if(initialized == 0){
        Eno = 0;
        phi = 0;
        PRINTF("DEBUG: Phi=%d\n", phi);
        return;
    }
    
    uint16_t  p;
    if (isDayTime(solar_energy)){
       p =  ((float)(slotCounter - dayTimeFirstSlot)
                /(float)(nightFirstTimeSlot-dayTimeFirstSlot))*100;
       PRINTF("dayTime\n");
  
    }else{
        p = ((float)(dayTimeFirstSlot + dayDuration - slotCounter)
                /(float)(dayTimeFirstSlot + dayDuration - nightFirstTimeSlot))
                *100;
        PRINTF("nightTime\n");
    }
    
    if (p == 0)
        p = 1;
    
    phi = ((float)p/(float)100) * Eno * 300;  
   PRINTF("p=%d\n", p);
   printf("Phi=%ld\n", phi+extraPhi);
   printf("Eno=%ld\n", Eno);
    
   
}

void lpm_set_unusedEnergy(uint16_t energy){
    
    if(initialized == 0){
        return;
    }
    printf("return_energy=%d\n", energy);
    
    if(energyConsumption - energy > 0){
        energyConsumption -= energy;
    }//Otherwise, all energy have been used

    int32_t deltaBattery = 0;
    
     //Calc Battery level for the next cycle
    if( preSolar > energyConsumption){ //Recharging 
        deltaBattery =
                + (rechargingEfficiency*(preSolar - energyConsumption))
                - batteryLeakage;       
    }else{ //Discharging 
        deltaBattery = preSolar - energyConsumption - batteryLeakage;
    }
 
   
    //deltaBattery can be positive in case of charging or negative in case of discharge
    PRINTF("DEBUG: DeltaBattery=%ld\n",deltaBattery);
    int32_t result =  batteryLevel + 300*(uint32_t)deltaBattery;
    
    if(result < 0){
        result = 0;
       
    }
 
    if(result > (uint32_t) batteryMax && result-batteryMax > 0){
        printf("wasted=%ld\n", result-batteryMax);
    } 
    
    batteryLevel = result;

    checkBatteryLevel();
}
void lpm_set_input(uint16_t solar_energy){
    //Reset parameters 
    newCycle();
    calcPhi(solar_energy);
  
    //The LPM needs at least one day to inilialize its parameters 
     if(initialized == 0){
        return;
    }
      
    //Calc energy budget for this cycle
    energyConsumption = solar_energy;
   //  printf("energyConsumption = solar_energy=%d\n", energyConsumption);
    energyConsumption +=  batteryLevel;
  //  printf("energyConsumption +=  batteryLevel=%d\n", energyConsumption);
    energyConsumption -= phi;
  //   printf("energyConsumption -=  phi=%d\n", energyConsumption);
    energyConsumption -= extraPhi;
  //   printf("energyConsumption -=  extraPhi=%d\n", energyConsumption);
    energyConsumption -= batteryLeakage;
  //   printf("energyConsumption -=  batteryLeakage=%d\n", energyConsumption);
  
    energyConsumption = checkConsumption(energyConsumption);
    //printf("energy allowed=%d\n", energyConsumption);
    
    preSolar = solar_energy;
    
    //Calc the energy budget for this time slot
    if(solar_energy > energyConsumption){ //Recharging 
        energyConsumption =  solar_energy 
                + ((float)(batteryLevel - phi - extraPhi)/(float)rechargingEfficiency) 
                - batteryLeakage;
      
        energyConsumption = checkConsumption(energyConsumption);  
    }  
}


uint16_t lpm_get_energy_budget(){
    
    //return 100;
    return (uint16_t) energyConsumption;
}

  
  
uint32_t lpm_get_battery_level(){
    
    return batteryLevel;
}
