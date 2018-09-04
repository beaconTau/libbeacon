#include "bbb_ain.h" 
#include <stdio.h> 
#include <stdlib.h>

int main(int nargs, char **args) 
{
  printf("%f\n",bbb_ain_V(atoi(args[1]))); 
  return 0; 
}
