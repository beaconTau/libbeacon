#include <stdio.h>
#include "bbb_ain.h" 

#define AINPATH "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw" 



int bbb_ain_raw(int ain)
{
  int val; 
  FILE * f; 
  char buf[2*sizeof(AINPATH)]; 
  if (ain < 0 || ain > 6) return -1; 
  sprintf(buf, AINPATH, ain); 
  f = fopen(buf,"r"); 
  fscanf(f,"%d", &val); 
  fclose(f); 
  return val; 
}


float bbb_ain_V(int ain)
{
  int raw = bbb_ain_raw(ain);  
  if (raw < 0) return -1;

  /* Eric's conversion factors for BEACON */
  float adc = 1.8*((float) raw)/4096.0;
  return adc;
  /* float temp = (adc - 1.8583)/-0.01167; */
  /* return 1.5*temp; */
}
