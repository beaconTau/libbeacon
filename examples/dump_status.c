#include "beacon.h" 
#include <stdio.h> 
#include "zlib.h"
#include "string.h" 


int main(int nargs, char ** args) 
{

  if (nargs < 2) 
  {
    fprintf(stderr,"dump_status status.dat"); 
    return 1; 
  }



  int is_zipped = strstr(args[1],".gz") != 0; 

  if (!is_zipped)
  {
    FILE * f = fopen(args[1], "r"); 
    beacon_status_t status;


    while (!beacon_status_read(f,&status))
    {
      beacon_status_print(stdout, &status); 
    }

    fclose(f); 
  }
  else
  {
    gzFile f = gzopen(args[1], "r"); 
    beacon_status_t status;

    while (!beacon_status_gzread(f,&status))
    {
      beacon_status_print(stdout, &status); 
    }

    gzclose(f); 

  }

  return 0; 





}
