#ifndef _nuphasehk_h 
#define _nuphasehk_h 

#include "nuphase.h" 

/** \file nuphasehk.h
 * 
 *  NuPhase housekeeping functions. 
 *
 *   To get current housekeeping info, use the nuphase_hk method. 
 *
 *   To set the ASPS power state, use nuphase_set_asps_power_state
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
int nuphase_hk(nuphase_hk_t * hk); 


/** Set the GPIO power state. For the FPGA's to be on, the relevant ASPS power state must also be enabled
 * Note that even for things with inverted state (active low instead of active high), you should use the 
 * logical state here. 
 *
 * The mask allows you to only set some of the pins (and leave the others unchanged) 
 **/ 
int nuphase_set_gpio_power_state ( nuphase_gpio_power_state_t state, nuphase_gpio_power_state_t mask); 

/** Reboots the FPGA's via the gpio's */
int nuphase_reboot_fpga_power(int sleep_after_off, int sleep_after_master_on); 


#endif
