#include "beacon.h" 
#include <stdio.h> 
#include "zlib.h"
#include "string.h" 


int main(int nargs, char ** args) 
{

  if (nargs < 2) 
  {
    fprintf(stderr,"dump_headers headers.dat"); 
    return 1; 
  }



  int is_zipped = strstr(args[1],".gz") != 0; 

  if (!is_zipped)
  {
    FILE * f = fopen(args[1], "r"); 
    beacon_header_t hd;


    while (!beacon_header_read(f,&hd))
    {
      beacon_header_print(stdout, &hd); 
    }

    fclose(f); 
  }
  else
  {
    gzFile f = gzopen(args[1], "r"); 
    beacon_header_t hd;

    while (!beacon_header_gzread(f,&hd))
    {
      beacon_header_print(stdout, &hd); 
    }

    gzclose(f); 

  }

  return 0; 





}
