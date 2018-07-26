#include "nuphasehk.h"
#include "bbb_ain.h" 
#include "bbb_gpio.h" 

#include <time.h> 
#include <stdio.h> 
#include <sys/statvfs.h> 
#include <stdlib.h>
#include <termios.h> 
#include <unistd.h> 
#include <string.h> 
#include <fcntl.h> 
#include <sys/ioctl.h> 



//TODO These are almost certainly wrong now!!! 
#define MASTER_TEMP_AIN 0
#define MASTER_POWER_GPIO 46
#define COMM_GPIO 60


// Global HK settings 
//---------------------------------------------

static int gpios_are_setup = 0;

//---------------------------------------------
//   device handles for the GPIO pins 
//---------------------------------------------
static bbb_gpio_pin_t * master_fpga_ctl = 0; 
static bbb_gpio_pin_t * comm_ctl = 0; 


/** GPIO Setup
 *
 *  This just exports them
 *  
 **/ 
static int setup_gpio() 
{
  // take control of the gpio's 
  int ret = 0; 

  master_fpga_ctl = bbb_gpio_open(MASTER_POWER_GPIO);
  if (!master_fpga_ctl) ret+=1;  

  comm_ctl = bbb_gpio_open(COMM_GPIO); 
  if (!comm_ctl) ret+=4; 

  gpios_are_setup = 1; 
  return ret;
}


//---------------------------------------------------
// Read in the GPIO state
// -------------------------------------------------
static nuphase_gpio_power_state_t query_gpio_state() 
{

  if (!gpios_are_setup) setup_gpio(); 
  nuphase_gpio_power_state_t state = 0; 

  //master is on as an input, I think
  if (!master_fpga_ctl || bbb_gpio_get(master_fpga_ctl) )
  {
    state = state | NP_FPGA_POWER_MASTER; 
  }
  
  //active low 
  if (comm_ctl && bbb_gpio_get(comm_ctl) == 0)
  {
    state = state | NP_SPI_ENABLE; 
  }


  return state; 
}


//--------------------------------------
//temperature probe conversion 
//-------------------------------------
static float mV_to_C(float val_mV) 
{
  return (1858.3-2*val_mV)  * 0.08569; 
}



static uint32_t get_free_kB()
{
  char bitbucket[256]; 
  FILE * meminfo = fopen("/proc/meminfo","r"); 
  uint32_t available = 0; 

  //eat first line
  fgets(bitbucket, 256, meminfo); 
  //eat second line
  fgets(bitbucket, 256, meminfo); 


  fscanf(meminfo, "MemAvailable: %u kB", &available); 

  fclose(meminfo); 

  return available; 
}


//----------------------------------------
//The main hk update method 
//----------------------------------------
int nuphase_hk(nuphase_hk_t * hk) 
{

  /* first the ASPS-DAQ bits, using the specified method. */

  struct timespec now; 
  struct statvfs fs; 

  /* now, read in our temperatures*/ 
  hk->temp_master =  mV_to_C(bbb_ain_mV(MASTER_TEMP_AIN)) ;

  /* figure out the disk space  and memory*/ 
  statvfs("/", &fs); 
  hk->disk_space_kB = fs.f_bsize * (fs.f_bavail >> 10) ; 
  hk->free_mem_kB = get_free_kB(); 

  /* check our gpio state */ 
  hk->gpio_state = query_gpio_state()  ; 

  //get the time
  clock_gettime(CLOCK_REALTIME_COARSE, &now); 
  hk->unixTime = now.tv_sec; 
  hk->unixTimeMillisecs = now.tv_nsec / (1000000); 
  return 0; 

}


int nuphase_set_gpio_power_state ( nuphase_gpio_power_state_t state, nuphase_gpio_power_state_t mask) 
{
  if (! gpios_are_setup) setup_gpio(); 

  int ret = 0; 

  if (mask & NP_FPGA_POWER_MASTER) 
  {
    ret += !master_fpga_ctl || bbb_gpio_set( master_fpga_ctl, (state & NP_FPGA_POWER_MASTER)); 
  }

  if (mask & NP_SPI_ENABLE) 
  {
    //this one is active low
    ret += !comm_ctl || bbb_gpio_set( comm_ctl, !(state & NP_SPI_ENABLE) ); 
  }

  return ret; 
}


/** Sleep that resumes when interrupted by a signal */ 
static void smart_sleep(int amount) 
{
  while(amount) amount = sleep(amount);
}

///////////////////////////////////////////
////  FPGA reboot
////////////////////////////////////////////
int nuphase_reboot_fpga_power(int sleep_after_off, int sleep_after_master_on)
{
  if (!gpios_are_setup) setup_gpio(); 

  int ret = 0; 
  ret+=bbb_gpio_set(master_fpga_ctl, 0); 
  smart_sleep(sleep_after_off); 
  ret+=bbb_gpio_set(master_fpga_ctl, 1); 
  smart_sleep(sleep_after_master_on); 
  return ret; 
}


//-----------------------------------------
//    deinit
//-----------------------------------------
__attribute__((destructor)) 
static void nuphase_hk_destroy() 
{
  //do NOT unexport any of these!
  if (master_fpga_ctl) bbb_gpio_close(master_fpga_ctl,0); 
  if (comm_ctl) bbb_gpio_close(comm_ctl,0); 
}





