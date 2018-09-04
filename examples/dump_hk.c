#include "beacon.h" 
#include <stdio.h> 
#include "zlib.h"
#include "string.h" 


int main(int nargs, char ** args) 
{

  if (nargs < 2) 
  {
    fprintf(stderr,"dump_hk hk.dat"); 
    return 1; 
  }



  int is_zipped = strstr(args[1],".gz") != 0; 

  if (!is_zipped)
  {
    FILE * f = fopen(args[1], "r"); 
    beacon_hk_t hk;


    while (!beacon_hk_read(f,&hk))
    {
      beacon_hk_print(stdout, &hk); 
    }

    fclose(f); 
  }
  else
  {
    gzFile f = gzopen(args[1], "r"); 
    beacon_hk_t hk;

    while (!beacon_hk_gzread(f,&hk))
    {
      beacon_hk_print(stdout, &hk); 
    }

    gzclose(f); 

  }

  return 0; 





}
