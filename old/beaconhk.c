#include "beaconhk.h"
#include "bbb_ain.h" 
#include "bbb_gpio.h" 

#include <time.h> 
#include <stdio.h> 
#include <sys/statvfs.h> 
#include <stdlib.h>
#include <termios.h> 
#include <unistd.h> 
#include <string.h> 
#include <fcntl.h> 
#include <sys/ioctl.h> 
#include <curl/curl.h> 



#define BOARD_TEMP_AIN 6
#define ADC_TEMP_0_AIN 0 
//#define ADC_TEMP_1_AIN 2 

#define FRONTEND_IMON_AIN 5 
#define ADC_IMON_AIN 3 
#define ANT_IMON_AIN 1 
#define AUX_IMON_AIN 4 

#define MASTER_POWER_GPIO 46
#define COMM_GPIO 60


// Global HK settings 
//---------------------------------------------

static int gpios_are_setup = 0;

//---------------------------------------------
//   device handles for the GPIO pins 
//---------------------------------------------
static bbb_gpio_pin_t * master_fpga_ctl = 0; 
static bbb_gpio_pin_t * comm_ctl = 0; 


/** GPIO Setup
 *
 *  This just exports them
 *  
 **/ 
static int setup_gpio() 
{
  // take control of the gpio's 
  int ret = 0; 

  master_fpga_ctl = bbb_gpio_open(MASTER_POWER_GPIO);
  if (!master_fpga_ctl) ret+=1;  

  comm_ctl = bbb_gpio_open(COMM_GPIO); 
  if (!comm_ctl) ret+=4; 

  gpios_are_setup = 1; 
  return ret;
}


//---------------------------------------------------
// Read in the GPIO state
// -------------------------------------------------
static beacon_gpio_power_state_t query_gpio_state() 
{

  if (!gpios_are_setup) setup_gpio(); 
  beacon_gpio_power_state_t state = 0; 

  //master is on as an input, I think
  if (!master_fpga_ctl || bbb_gpio_get(master_fpga_ctl) )
  {
    state = state | BN_FPGA_POWER_MASTER; 
  }
  
  //active low 
  if (comm_ctl && bbb_gpio_get(comm_ctl) == 0)
  {
    state = state | BN_SPI_ENABLE; 
  }


  return state; 
}


//--------------------------------------
//temperature probe conversion 
//-------------------------------------
static float V_to_C(float val_V)
{
  /* return (1858.3-2*val_mV)  * 0.08569; */
  return (val_V - 1.8583)/-0.01167;
}


//--------------------------
// current conversion 
// ------------------------

static uint16_t V_to_mA(float val_V)
{
  float imon_res = 6800.e-6; 
  float imon_gain = 52.0 ; 
  float imon_offset = 0.8; //*1000;

  return 1000*((val_V /imon_res - imon_offset) / imon_gain);
}


static uint32_t get_free_kB()
{
  char bitbucket[256]; 
  FILE * meminfo = fopen("/proc/meminfo","r"); 
  uint32_t available = 0; 

  //eat first line
  fgets(bitbucket, 256, meminfo); 
  //eat second line
  fgets(bitbucket, 256, meminfo); 


  fscanf(meminfo, "MemAvailable: %u kB", &available); 

  fclose(meminfo); 

  return available; 
}



//---------------------------------------------------
// MATE3 parsing stuff, this is very naive. A better
// method might use a json parser or something like
// that 
//---------------------------------------------------

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

//this is our cURL callback that copies into our buffer
static size_t save_http(char * ptr, size_t size, size_t nmemb, void * user) 
{
  http_buf_t * http_buf = (http_buf_t*) user; 

  if (!http_buf->buf)  // we haven't allocated a buffer yet. let's do it. minimum of 16K or whatever cURL passes us, +1 for null byte
  {
    http_buf->size = size*nmemb < 16 * 1024 ? 1+ 16 * 1024 : size*nmemb + 1; 
    http_buf->buf = malloc(http_buf->size); 
    if (!http_buf->buf) return 0; 
  }

  // see if we need a bigger buffer. if we do, allocate twice more what cURL gave us 
  else if (http_buf->size < http_buf->pos + size * nmemb + 1) 
  {
    http_buf->size = http_buf->pos + size*nmemb *2 + 1; 
    http_buf->buf = realloc( http_buf->buf, http_buf->size); 
    if (!http_buf->buf) return 0; 
  }
  

  //copy into our buffer
//  printf("memcpy(%x, %x, %u)\n", http_buf + http_buf_pos, ptr, size*nmemb); 
  memcpy (http_buf->buf + http_buf->pos, ptr, size*nmemb); 
  http_buf->pos += size * nmemb; 
  http_buf->buf[http_buf->pos] = 0; // set the null byte
//  printf("save_http called, buf is :%s\n", http_buf); 

  return size * nmemb;  //if we don't return the size given, cURL gets angry
}



static float parse_json_number_like_an_idiot(const char * str, const char * key, const char * after)
{

  if (!str) return 0; 
  int offset = 0; 

  if (after) 
  {
    char * found_after = strstr(str,after);
    if (found_after) offset = found_after - str; 
  }


  char real_key[128]; // no key can possibly be longer than this? 
  snprintf(real_key, sizeof(real_key),"\"%s\": ", key); 

  char * start = strstr(str+offset, real_key); 
  if (!start) return 0; 

  char fmt[132]; 
  fmt[0]= 0; 
  strcat(fmt,real_key); 
  strcat(fmt,"%f"); 

  float f; 
  int found = sscanf(start, fmt, &f); 

  if (!found) return 0; 
  return f; 
}



static int parse_http(http_buf_t * buf, beacon_hk_t * hk) 
{
  
  if (!buf->size || !buf->buf) 
  {
    return 1; 
  }

  //once again, this does the dumbest possible things, and could be faster. but obviously don't matter
  //look for the inverter battery voltage
  //assume all inverters have "FX" in them
  float inv_batt_v = parse_json_number_like_an_idiot(buf->buf, "Batt_V", "\"FX\""); 
  ///assume the charge controller has "CC" in it
  float cc_batt_v = parse_json_number_like_an_idiot(buf->buf, "Batt_V","\"CC\""); 
  float ah = parse_json_number_like_an_idiot(buf->buf, "Out_AH","\"CC\""); 
  float kwh = parse_json_number_like_an_idiot(buf->buf, "Out_kWh","\"CC\""); 
  float pv = parse_json_number_like_an_idiot(buf->buf, "In_V","\"CC\""); 

  hk->inv_batt_dV = inv_batt_v *10; 
  hk->cc_batt_dV = cc_batt_v *10; 
  hk->pv_dV = pv*10; 
  hk->cc_daily_Ah = ah > 255 ? 255 : ah; 
  hk->cc_daily_hWh = kwh > 25.5 ? 255: kwh * 10; 
  return 0; 
}



void beacon_hk_set_mate3_address(const char * addr, int port)
{

  if (port) mate3_port = port; 
  if (mate3_addr) free(mate3_addr); 
  mate3_addr = 0; 
  asprintf(&mate3_addr, "http://%s:%d/Dev_status.cgi?Port=0", addr, mate3_port); 
}


static int http_update(beacon_hk_t *hk)
{
  if (!mate3_addr) goto fail;
  if (!curl) 
  {
    curl = curl_easy_init(); 
    if (!curl) goto fail;
  }

  http_buf.pos = 0; 

  curl_easy_setopt(curl, CURLOPT_URL, mate3_addr); 
  curl_easy_setopt(curl, CURLOPT_HTTPGET,1); 
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,1); 
  curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, save_http); 
  curl_easy_setopt( curl, CURLOPT_WRITEDATA, &http_buf); 
  if (curl_easy_perform(curl)) goto fail; 
  return parse_http(&http_buf, hk); 

fail: 
  hk->inv_batt_dV = 0; 
  hk->cc_batt_dV = 0; 
  hk->pv_dV = 0; 
  hk->cc_daily_Ah = 0;
  hk->cc_daily_hWh = 0;
  return 1; 

}


//----------------------------------------
//The main hk update method 
//----------------------------------------
int beacon_hk(beacon_hk_t * hk) 
{

  /* first the ASPS-DAQ bits, using the specified method. */

  struct timespec now; 
  struct statvfs fs; 

  /* now, read in our temperatures*/ 
  hk->temp_board =  V_to_C(1.5*bbb_ain_V(BOARD_TEMP_AIN)) ;
  hk->temp_adc =  V_to_C(1.5*bbb_ain_V(ADC_TEMP_0_AIN)) ;
  /* hk->temp_adc_1 =  mV_to_C(1.5*bbb_ain_mV(ADC_TEMP_1_AIN)) ; */


  /* and the currents */ 
  hk->adc_current = V_to_mA(bbb_ain_V(ADC_IMON_AIN));
  hk->ant_current = V_to_mA(bbb_ain_V(ANT_IMON_AIN));
  hk->aux_current = V_to_mA(bbb_ain_V(AUX_IMON_AIN));
  hk->frontend_current = V_to_mA(bbb_ain_V(FRONTEND_IMON_AIN));


  /* figure out the disk space  and memory*/ 
  statvfs("/", &fs); 
  hk->disk_space_kB = fs.f_bsize * (fs.f_bavail >> 10) ; 
  hk->free_mem_kB = get_free_kB(); 

  /* check our gpio state */ 
  hk->gpio_state = query_gpio_state()  ; 

  //get the time
  clock_gettime(CLOCK_REALTIME_COARSE, &now); 
  hk->unixTime = now.tv_sec; 
  hk->unixTimeMillisecs = now.tv_nsec / (1000000); 


  //load the http stuff
  return http_update(hk); 

}


int beacon_set_gpio_power_state ( beacon_gpio_power_state_t state, beacon_gpio_power_state_t mask) 
{
  if (! gpios_are_setup) setup_gpio(); 

  int ret = 0; 

  if (mask & BN_FPGA_POWER_MASTER) 
  {
    ret += !master_fpga_ctl || bbb_gpio_set( master_fpga_ctl, (state & BN_FPGA_POWER_MASTER)); 
  }

  if (mask & BN_SPI_ENABLE) 
  {
    //this one is active low
    ret += !comm_ctl || bbb_gpio_set( comm_ctl, !(state & BN_SPI_ENABLE) ); 
  }

  return ret; 
}


/** Sleep that resumes when interrupted by a signal */ 
static void smart_sleep(int amount) 
{
  while(amount) amount = sleep(amount);
}

///////////////////////////////////////////
////  FPGA reboot
////////////////////////////////////////////
int beacon_reboot_fpga_power(int sleep_after_off, int sleep_after_master_on)
{
  if (!gpios_are_setup) setup_gpio(); 

  int ret = 0; 
  ret+=bbb_gpio_set(master_fpga_ctl, 0); 
  smart_sleep(sleep_after_off); 
  ret+=bbb_gpio_set(master_fpga_ctl, 1); 
  smart_sleep(sleep_after_master_on); 
  return ret; 
}


//-----------------------------------------
//    deinit
//-----------------------------------------
__attribute__((destructor)) 
static void beacon_hk_destroy() 
{
  //do NOT unexport any of these!
  if (master_fpga_ctl) bbb_gpio_close(master_fpga_ctl,0); 
  if (comm_ctl) bbb_gpio_close(comm_ctl,0); 
  if (mate3_addr) free(mate3_addr); 
  if (curl) curl_easy_cleanup(curl); 
}





