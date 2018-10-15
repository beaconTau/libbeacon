
#include "beacon.h"
#include <curl/curl.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <string.h> 


static int mate3_port = 8080; 
static char * mate3_addr = 0;
static CURL * curl = 0; 
typedef struct http_buf
{
  char * buf; 
  size_t pos; 
  size_t size; 
} http_buf_t; 


// if we ever wanted to make this nonreentrant, it's not so hard... 
static http_buf_t http_buf; 



int main(int nargs, char ** args) 
{

  char * url = "162.252.89.77"; 
  if (nargs > 1) url = args[1]; 
  beacon_hk_set_mate3_address(url,0); 

  beacon_hk_t hk; 
  memset(&hk,0,sizeof(hk)); 


  http_update(&hk); 
  beacon_hk_print(stdout, &hk); 

  return 0; 






}
