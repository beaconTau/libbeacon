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

  uint16_t thresh=500;
  if (nargs > 1) thresh = atoi(args[1]); 
  uint32_t mask=0xfffff;
  if (nargs>2) mask=atoi(args[2]);


  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], 0); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], 0); 

  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 
  flower8_trigger_enables_t t_enables = {.enable_coinc = 0, .enable_phased=1};  

  flower8_set_phased_trigger_mask(b,mask,0);
  beacon_status_t st;
  flower8_get_trigger_enables(b,&t_enables);
  //printf("coinc %i, phased %i\n",t_enables.enable_coinc,t_enables.enable_phased);

  flower8_set_trigger_enables(b,t_enables);

  uint16_t threshs[20];
  for(int i =0;i<20;i++) 
  {
    threshs[i]=thresh;
    printf("thresh %i\n",threshs[i]);
  }
  printf("thresh %i\n",thresh);
  flower8_set_phased_thresholds(b,threshs,threshs);
  flower8_bouquet_dump(stdout,b);
  beacon_fill_status(b,&st); 
  beacon_status_print(stdout, &st); 

  flower8_bouquet_discard(b,1);
  return 0; 

}

