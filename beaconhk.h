#ifndef _beaconhk_h 
#define _beaconhk_h 

#include "beacon.h" 

/** \file beaconhk.h
 * 
 *  Beacon housekeeping functions. 
 *
 *   To get current housekeeping info, use the beacon_hk method. 
 *
 *   To set the GPIO power states  
 *
 *
 *  This library uses static internal buffers so it is not thread-safe nor reentrant! 
 *
 *  Cosmin Deaconu
 *  <cozzyd@kicp.uchicago.edu> 
 *
 */

/** Fills in this hk struct, using the specified method to communicate with the ASPS-DAQ */ 
int beacon_hk(beacon_hk_t * hk); 


/** Set the GPIO power state. For the FPGA's to be on, the relevant ASPS power state must also be enabled
 * Note that even for things with inverted state (active low instead of active high), you should use the 
 * logical state here. 
 *
 * The mask allows you to only set some of the pins (and leave the others unchanged) 
 **/ 
int beacon_set_gpio_power_state ( beacon_gpio_power_state_t state, beacon_gpio_power_state_t mask); 

/** Reboots the FPGA's via the gpio's */
int beacon_reboot_fpga_power(int sleep_after_off, int sleep_after_master_on); 


#endif
