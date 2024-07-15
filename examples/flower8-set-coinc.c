#include <stdio.h>
#include <stdlib.h>
#define _BEACON_
#include "flower8.h" 
#include "beacon.h"
#include <zlib.h> 
#include <string.h> 



static char * devM = "/dev/spidev1.0";
static char * devS = "/dev/spidev0.0"; 
int spi_en[2]  = {-61,0}; 
int gpio_int[2]  = {44,89}; 



int main (int nargs, char ** args)
{

  uint8_t thresh=127;
  if (nargs > 1) thresh = atoi(args[1]); 


  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], 0); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], 0); 

  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 
  flower8_trigger_enables_t t_enables = {.enable_coinc = 1, .enable_phased=0};  
  flower8_coinc_trigger_config_t tcfg = {.vpp_mode = 1, .num_coinc = 0, .window = 10 }; 

  beacon_status_t st;

  //printf("coinc %i, phased %i\n",t_enables.enable_coinc,t_enables.enable_phased);

  flower8_set_coinc_trigger_mask(b,0xff);
  flower8_configure_coinc_trigger(b, tcfg); 
  flower8_set_trigger_enables(b,t_enables);

  uint8_t threshs[8]={thresh,thresh,thresh,thresh,thresh,thresh,thresh,thresh};
  flower8_set_coinc_thresholds(b,threshs,threshs,0xff);
  flower8_bouquet_dump(stdout,b);
  beacon_fill_status(b,&st); 
  beacon_status_print(stdout, &st);

  flower8_bouquet_discard(b,1); 
  return 0; 

}

