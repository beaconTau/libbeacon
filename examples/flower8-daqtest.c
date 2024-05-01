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

  int force = 0;
  int rf = 1; 
  if (nargs > 1 ) force =atoi(args[1]); 
  if (nargs > 2) rf = atoi(args[2]);  
  uint32_t flags = 0; 
  flags |=FLOWER8_ENABLE_LOCKING; 
  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], flags); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], flags); 
//  flower8_equalize(M,1,0,FLOWER8_EQUALIZE_VERBOSE); 
//  flower8_equalize(S,1,0,FLOWER8_EQUALIZE_VERBOSE); 
  uint8_t gains[8] = { 
	  FLOWER8_GAIN_10X, FLOWER8_GAIN_10X,
	  FLOWER8_GAIN_10X, FLOWER8_GAIN_10X,
	  FLOWER8_GAIN_10X, FLOWER8_GAIN_10X,
	  FLOWER8_GAIN_10X, FLOWER8_GAIN_10X}; 

  flower8_set_gains(M,gains);
  flower8_set_gains(S,gains);
 
  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 
  flower8_trigger_enables_t t_enables = {.enable_coinc = !!rf}; 
  flower8_set_trigger_enables(b,t_enables); 
  flower8_coinc_trigger_config_t tcfg = {.vpp_mode = 0, .num_coinc = 0, .window = 10 }; 
  flower8_configure_coinc_trigger(b, tcfg); 
  uint8_t trig_thresh[8] = { 10,10,10,10,10,10,10,10}; 
  uint8_t servo_thresh[8] = { 8,8,8,8,8,8,8,8}; 
  flower8_set_coinc_thresholds(b, trig_thresh, servo_thresh, 0xff); 

  beacon_header_t hd;
  beacon_event_t ev;
  beacon_status_t st; 

  FILE * hdf = fopen("header.dat", "w"); 
  FILE * evf = fopen("event.dat", "w"); 
  FILE * stf = fopen("status.dat", "w"); 

  beacon_fill_status(b,&st); 
  beacon_status_print(stdout, &st); 
  for (int i = 0; i < 10; i++) 
  {
    while (beacon_wait_for_and_fill_event(b,&hd,&ev, 100))
    {
      if (force)
      {
        printf("Sending force trigger\n"); 
        flower8_force_trigger(b); 
      }
    }
    beacon_header_write(hdf, &hd);
    beacon_event_write(evf, &ev);
    beacon_status_write(stf, &st);
    beacon_header_print(stdout, &hd); 
//    beacon_event_print(stdout, &ev,','); 
  }

  beacon_fill_status(b,&st); 
  beacon_status_print(stdout, &st); 

  flower8_bouquet_discard(b,1); 
  fclose(hdf); 
  fclose(evf); 
  fclose(stf); 
  return 0; 

}

