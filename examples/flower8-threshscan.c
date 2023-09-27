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

  int gain_code = FLOWER8_GAIN_10X; 
  if (nargs > 1) gain_code = atoi(args[1]); 
  int window = 10; 
  if (nargs > 2) window = atoi(args[2]); 
  int ncoinc = 0; 
  if (nargs > 3) ncoinc = atoi(args[3]); 

  printf("Using gain code %d, window %d, ncoinc %d\n", gain_code, window, ncoinc); 

  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], 0); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], 0); 
//  flower8_equalize(M,1,0,FLOWER8_EQUALIZE_VERBOSE); 
//  flower8_equalize(S,1,0,FLOWER8_EQUALIZE_VERBOSE); 
  uint8_t gains[8] = { 
	  gain_code, gain_code,
	  gain_code, gain_code,
	  gain_code, gain_code,
	  gain_code, gain_code}; 

  flower8_set_gains(M,gains);
  flower8_set_gains(S,gains);
 
  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 
  flower8_trigger_enables_t t_enables = {.enable_coinc = 1}; 
  flower8_set_trigger_enables(b,t_enables); 
  flower8_trigger_config_t tcfg = {.vpp_mode = 0, .num_coinc = ncoinc, .window = window }; 

  beacon_status_t st; 
  flower8_configure_trigger(b, tcfg); 
  FILE * stf = fopen("scan.dat","w"); 

  FILE * result = fopen("scan-result.txt","w"); 

  fprintf(result,"threshold,rate\n"); 
  for (int ithresh = 2; ithresh <= 50; ithresh+=1)
  {
	  int sthresh =ithresh-1;
	  uint8_t trig_thresh[8] = { ithresh,ithresh,ithresh,ithresh,ithresh,ithresh,ithresh,ithresh}; 
	  uint8_t servo_thresh[8] = { sthresh,sthresh,sthresh,sthresh,sthresh,sthresh,sthresh,sthresh};
	  flower8_set_thresholds(b, trig_thresh, servo_thresh, 0xff); 
	  printf("Setting trigger thresholds to %hhu, servo_thresholds to %hhu\n", ithresh, sthresh); 
	  sleep(2); 
	  beacon_fill_status(b,&st); 
	  beacon_status_print(stdout, &st); 
          beacon_status_write(stf, &st);
	  fprintf(result,"%d,%d\n%d,%d\n", sthresh, st.global_servo_scalers[2], ithresh, st.global_scalers[2]);

    }

  flower8_bouquet_discard(b,1); 
  fclose(stf); 
  return 0; 

}

