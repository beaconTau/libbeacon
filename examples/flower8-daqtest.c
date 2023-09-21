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
 
  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 

  beacon_header_t hd;
  beacon_event_t ev;
  beacon_status_t st; 
  beacon_read_status(b,&st); 
  beacon_status_print(stdout, &st); 
  while (!beacon_wait_for_and_read_event(b,&hd,&ev, 1));
  beacon_header_print(stdout, &hd); 
  beacon_event_print(stdout, &ev,','); 
  flower8_bouquet_discard(b,1); 
  return 0; 

}

