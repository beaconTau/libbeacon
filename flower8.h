#ifndef __flower8_h__
#define __flower8_h__ 



/**
 * FLOWER8 API (BEACON Experiment) 
 * Cosmin Deaconu
 * cozzyd@kicp.uchicago.edu 
 *
 *
 * A few functions operate on a single board, such as initialization, but most operate on a "bouquet" which is one or more board. 
 * For most use cases, you want to initailize each board and then pass it to the bouquet. 
 */ 

#ifdef _BEACON_
#include "beacon.h" 
#endif 

#include <stdio.h>
#include <stdint.h>

#define FLOWER8_MAX_TRIG_CHAN 8 
#define FLOWER8_MAX_TRIG_BEAMS 42


typedef struct flower8_dev flower8_dev_t; 
typedef struct flower8_bouquet flower8_bouquet_t; 

//a bouquet is one more mor flower8 boards
//it will automatically set up synchronization and such



typedef struct flower8_coinc_scaler_group
{
  uint16_t trig_coinc; 
  uint16_t trig_per_chan[FLOWER8_MAX_TRIG_CHAN]; 
  uint16_t servo_coinc; 
  uint16_t servo_per_chan[FLOWER8_MAX_TRIG_CHAN]; 

} flower8_coinc_scaler_group; 

typedef struct flower8_scaler_phased_group
{
  uint16_t trig_beam; 
  uint16_t trig_per_beam[FLOWER8_MAX_TRIG_BEAMS]; 
  uint16_t servo_beam; 
  uint16_t servo_per_beam[FLOWER8_MAX_TRIG_BEAMS]; 

} flower8_scaler_phased_group; 


typedef enum 
{
  FLOWER8_SCAL_100mHz =1, 
  FLOWER8_SCAL_100Hz =2
} flower8_variable_scaler_type_t; 


typedef struct flower8_daqstatus
{
  flower8_scaler_coinc_group s_1Hz; 
  flower8_scaler_coinc_group s_1Hz_gated; 
  flower8_scaler_coinc_group s_100mHz; //100 Hz or 100 mHz, depending on scaler_type
  flower8_scaler_phased_group s_1Hz; 
  flower8_scaler_phased_group s_1Hz_gated; 
  flower8_scaler_phased_group s_100mHz; //100 Hz or 100 mHz, depending on scaler_type
  uint64_t ncycles : 48; 
  uint16_t scaler_counter_1Hz :  16; 
  uint64_t cycle_counter; 
  uint8_t trig_thresholds[FLOWER8_MAX_TRIG_CHAN]; 
  uint8_t servo_thresholds[FLOWER8_MAX_TRIG_CHAN]; 
  uint16_t trig_thresholds[FLOWER8_MAX_TRIG_BEAMS]; 
  uint16_t servo_thresholds[FLOWER8_MAX_TRIG_BEAMS]; 
  double when;
  flower8_variable_scaler_type_t scaler_speed; // type of scalers
} flower8_daqstatus_t; 

typedef enum flower8_mode
{
  FLOWER8_MODE_M,
  FLOWER8_MODE_S
} flower8_mode_t; 

typedef struct flower8_coinc_trigger_config
{
  uint8_t window; 
  uint8_t num_coinc; 
  uint8_t vpp_mode; 
}flower8_coinc_trigger_config_t; 

typedef struct flower8_phased_trigger_config
{
  //empty for now
}flower8_phased_trigger_config_t; 



typedef struct flower8_event_metadata
{
  uint32_t event_number; 
  uint32_t trig_number; 
  uint32_t pps_count; 
  uint64_t timestamp[2]; 
  uint16_t buflen; 
  uint8_t trig_type : 4; 
  uint8_t pps : 1;
  uint8_t trig_channels; 
  uint32_t trig_beams_lower; 
  uint32_t trig_channels_upper; 

} flower8_event_metadata_t; 


enum 
{
  FLOWER8_ENABLE_LOCKING=1
} e_flower8_flags; 



/** Assumes already initialixed! 
 * @param spi_device the name of the SPI device (e.g. /dev/spidev2.0 ) 
 * @param spi_enable name of the spi enable gpio (or 0 for none). Use negative for active low. 
 * @param trig_gpio the number of the gpio interrupt
 * */ 
flower8_dev_t *flower8_open(const char * spi_device, int spi_enable, int trig_gpio, int flags); 
flower8_bouquet_t *flower8_bouquet_prepare(flower8_dev_t * M, flower8_dev_t *S); 

flower8_dev_t* flower8_bouquet_get_M(flower8_bouquet_t * b); 
flower8_dev_t* flower8_bouquet_get_S(flower8_bouquet_t * b); 

void flower8_bouquet_set_readmask(flower8_bouquet_t *b ,uint16_t mask); 

int flower8_bouquet_reset(flower8_bouquet_t *b); 

void flower8_set_buffer_length(flower8_bouquet_t *b, uint16_t len); 
void flower8_set_event_number_offset(flower8_bouquet_t *b, uint64_t offs); 

//this will discard the bouquet. Whether or not the flower boards are clsoed depends on if destroy_flowers iw 1
int flower8_bouquet_discard(flower8_bouquet_t * bouquet, int destroy_flowers); 
int flower8_close(flower8_dev_t * dev); 

//
int flower8_dump(FILE* f, flower8_dev_t *dev); 
int flower8_bouquet_dump(FILE* f, flower8_bouquet_t * b); 

int flower8_configure_trigger(flower8_bouquet_t * b, flower8_trigger_config_t cfg); 

int flower8_set_thresholds(flower8_bouquet_t *b, const uint8_t * trigger_thresholds, const uint8_t * servo_thresholds, uint8_t mask); 

int flower8_fill_daqstatus(flower8_bouquet_t *dev, flower8_daqstatus_t * st); 
int flower8_set_variable_scaler_speed(flower8_bouquet_t * b, flower8_variable_scaler_type_t scal); 

int flower8_fill_metadata(flower8_bouquet_t *b,flower8_event_metadata_t* meta); 
int flower8_read_waveforms(flower8_dev_t * dev, int nsamps, uint8_t ** dest);
int flower8_set_pretrigger(flower8_bouquet_t *b, uint8_t pretrigger); 

int flower8_force_trigger(flower8_bouquet_t *b); 
int flower8_event_poll(flower8_bouquet_t * b, int * avail); 
int flower8_event_wait(flower8_bouquet_t * b, int  timeout); 

int flower8_buffer_clear(flower8_bouquet_t * dev); 


enum 
{
  FLOWER8_GAIN_1X, 
  FLOWER8_GAIN_1_25X, 
  FLOWER8_GAIN_2X, 
  FLOWER8_GAIN_2_5X, 
  FLOWER8_GAIN_4X, 
  FLOWER8_GAIN_5X, 
  FLOWER8_GAIN_8X, 
  FLOWER8_GAIN_10X, 
  FLOWER8_GAIN_12_5X, 
  FLOWER8_GAIN_16X, 
  FLOWER8_GAIN_20X, 
  FLOWER8_GAIN_25X, 
  FLOWER8_GAIN_32X, 
  FLOWER8_GAIN_50X, 
  FLOWER8_GAIN_TOO_HIGH
} FLOWER8_gain_codes; 

int flower8_set_gains(flower8_dev_t * dev, const uint8_t * gain_codes);


typedef union flower8_word
{
  uint32_t word; 
  uint8_t bytes[4]; 
}flower8_word_t; 

int flower8_read_register(flower8_dev_t*dev, uint8_t addr, flower8_word_t * result); 
int flower8_read_registers(flower8_dev_t*dev, int nreg,  const uint8_t  * addr, flower8_word_t * results); 
   
typedef struct flower8_pulse_opts
{
  uint8_t enable; 
  uint8_t sync; 
  uint8_t slow_rate; 
} flower8_pulse_opts_t; 

int flower8_pulse(flower8_dev_t*dev, flower8_pulse_opts_t opt); 


enum 
{
  FLOWER8_EQUALIZE_EXCLUDE_CH0 = 1, 
  FLOWER8_EQUALIZE_EXCLUDE_CH1 = 2, 
  FLOWER8_EQUALIZE_EXCLUDE_CH2 = 4, 
  FLOWER8_EQUALIZE_EXCLUDE_CH3 = 8, 
  FLOWER8_EQUALIZE_EXCLUDE_CH4 = 16, 
  FLOWER8_EQUALIZE_EXCLUDE_CH5 = 32, 
  FLOWER8_EQUALIZE_EXCLUDE_CH6 = 64, 
  FLOWER8_EQUALIZE_EXCLUDE_CH7 = 128, 
  FLOWER8_EQUALIZE_VERBOSE = 0x80000000
}e_flower8_equalize_opts; 

int flower8_equalize(flower8_dev_t*dev, float target_rms, uint8_t * gain_codes, uint32_t opts); 

typedef struct  flower8_trigger_enables
{
  uint8_t enable_pps : 1; 
  uint8_t enable_coinc : 1; 
  uint8_t enable_phased : 1; 

} flower8_trigger_enables_t; 



// This is used to determine what forms internal triggers on the FLOWER, though the coinc is required to pass it to trigout too since otherwise it's not formed. 
int flower8_set_trigger_enables(flower8_bouquet_t * dev, flower8_trigger_enables_t enables); 
int flower8_get_trigger_enables(flower8_bouquet_t * dev, flower8_trigger_enables_t *enables); 

int flower8_set_trigger_mask(flower8_bouquet_t * dev, uint8_t mask); 

/**Set the delayed PPS delay. The delay is in multiples of 40 ns*/
int flower8_set_delayed_pps_delay(flower8_bouquet_t * dev, uint32_t delay); 
int flower8_get_delayed_pps_delay(flower8_bouquet_t * dev, uint32_t* delay); 


int flower8_get_fwversion(flower8_dev_t *dev, uint8_t *major, uint8_t *minor, uint8_t *rev, uint16_t *year, uint8_t *month, uint8_t *day); 

#ifdef _BEACON_ 
int beacon_wait_for_and_fill_event(flower8_bouquet_t * b, beacon_header_t *hd, beacon_event_t* ev, int timeout); 
int beacon_fill_status(flower8_bouquet_t *b, beacon_status_t * st); 

#endif

#endif

