#include <stdio.h>
#include <stdlib.h>
#define _BEACON_
#include "flower8.h" 
#include "beacon.h"
#include <string.h> 



static char * devM = "/dev/spidev1.0";
static char * devS = "/dev/spidev0.0"; 
int spi_en[2]  = {-61,0}; 
int gpio_int[2]  = {44,89}; 



int main ()
{

  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], 0); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], 0); 
  flower8_equalize(M,5,0,FLOWER8_EQUALIZE_VERBOSE); 
  flower8_equalize(S,5,0,FLOWER8_EQUALIZE_VERBOSE); 
 
  
  flower8_close(S); 
  flower8_close(M); 
  return 0; 

}

