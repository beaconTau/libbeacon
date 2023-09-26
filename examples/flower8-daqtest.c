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
//  flower8_equalize(M,1,0,FLOWER8_EQUALIZE_VERBOSE); 
//  flower8_equalize(S,1,0,FLOWER8_EQUALIZE_VERBOSE); 
 
  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 

  beacon_header_t hd;
  beacon_event_t ev;
  beacon_status_t st; 
  beacon_fill_status(b,&st); 
  beacon_status_print(stdout, &st); 
  for (int i = 0; i < 10; i++) 
  {
	  while (beacon_wait_for_and_fill_event(b,&hd,&ev, 100))
	  {
	    flower8_force_trigger(b); 
	  }
	  beacon_header_print(stdout, &hd); 
	  beacon_event_print(stdout, &ev,','); 
  }
  flower8_bouquet_discard(b,1); 
  return 0; 

}

