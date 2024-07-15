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

  flower8_dev_t * M = flower8_open(devM, spi_en[0], gpio_int[0], 0); 
  flower8_dev_t * S = flower8_open(devS, spi_en[1], gpio_int[1], 0); 
  //flower8_equalize(M,5,0,FLOWER8_EQUALIZE_VERBOSE); 
  //flower8_equalize(S,1,0,FLOWER8_EQUALIZE_VERBOSE); 
  //uint8_t gains[8] = { 
  //	  gain_code, gain_code,
  // 	  gain_code, gain_code,
  // 	  gain_code, gain_code,
  //	  gain_code, gain_code}; 

  //flower8_set_gains(M,gains);
  //flower8_set_gains(S,gains);
 
  flower8_bouquet_t * b = flower8_bouquet_prepare(M,S); 
  flower8_bouquet_dump(stdout,b);
  flower8_trigger_enables_t t_enables = {.enable_coinc = 0, .enable_phased=1}; 
  flower8_set_trigger_enables(b,t_enables); 

  beacon_status_t st; 
  flower8_set_phased_trigger_mask(b,0xfffff,0);

  FILE * stf = fopen("phased_scan.dat","w"); 

  FILE * result = fopen("phased-scan-result.txt","w"); 

  fprintf(result,"threshold,rate\n"); 
  for (int ithresh = 200; ithresh <= 1000; ithresh+=25)
  {
	  int sthresh =ithresh*0.9;
	  uint16_t trig_thresh[20];
	  uint16_t servo_thresh[20];
          for(int i =0;i<20;i++)
          {
            trig_thresh[i]=ithresh;
            servo_thresh[i]=sthresh;
          }
	  flower8_set_phased_thresholds(b, trig_thresh, servo_thresh); 
	  printf("Setting trigger thresholds to %hhu, servo_thresholds to %hhu\n", ithresh, sthresh); 
	  sleep(2); 
	  beacon_fill_status(b,&st); 
	  beacon_status_print(stdout, &st); 
          beacon_status_write(stf, &st);
	  fprintf(result,"%d,%d\n%d,%d\n", sthresh, st.global_phased_servo_scalers[2], ithresh, st.global_phased_trig_scalers[2]);

    }

  flower8_bouquet_discard(b,1); 
  fclose(stf); 
  return 0; 

}

