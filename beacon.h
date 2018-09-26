#ifndef _beacon_h
#define _beacon_h

/**
 * Hacks for pybind11, which prefers strongly typed cpp things
 *
 */
#ifndef PYBIND11_NAMESPACE
#define ARRAY1D(type, name, nX)         type name[nX]
#define ARRAY2D(type, name, nX, nY)     type name[nX][nY]
#define ARRAY3D(type, name, nX, nY, nZ) type name[nX][nY][nZ]
#else
#include <array>
#define ARRAY1D(type, name, nX)                                         std::array<type, nX> name
#define ARRAY2D(type, name, nX, nY)                     std::array<std::array<type, nY>, nX> name
#define ARRAY3D(type, name, nX, nY, nZ) std::array<std::array<std::array<type, nZ>, nY>, nX> name
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** \file beacon.h 
 *
 * Include file for working with beacon data. 
 
 * Cosmin Deaconu <cozzyd@kicp.uchicago.edu> 
 *
 *
 * This header defines structures, constants and some utility functions for
 * beacon.
 * 
 * Note that these are in-memory structs. The on-disk format does differ.  For
 * structs that are meant to be persisted (headers and events), use the
 * beacon_X_write/read() functions. 
 * 
 * In particular, you should use the utility functions here to read/write binary
 * beacon data from disk as it handles the versioning properly. 
 *
 */

#include <time.h>
#include <stdint.h>
#include <stdio.h> 
#include <zlib.h>


/** The number of channels per board */
#define BN_NUM_CHAN 8 

/** The number of buffers */
#define BN_NUM_BUFFER 4 

/** The maximum length of a waveform */ 
#define BN_MAX_WAVEFORM_LENGTH 4096  

//master + slave 
#define BN_MAX_BOARDS 1  

/** The number of trigger beams available */ 
#define BN_NUM_BEAMS 24 

#define BN_NUM_SCALERS 3

/** Error codes for read/write */ 
typedef enum 
{
BN_ERR_CHECKSUM_FAILED  = 0xbadadd,  //!< checksum failed while reading
BN_ERR_NOT_ENOUGH_BYTES = 0xbadf00d, //!< did not write or read enough bytes
BN_ERR_WRONG_TYPE       = 0xc0fefe , //!< got nonsensical type
BN_ERR_BAD_VERSION      = 0xbadbeef  //!< version number not understood
} beacon_io_error_t; 


 
/**  Trigger types */ 
typedef enum beacon_trigger_type 
{
  BN_TRIG_NONE,   //<! Triggered by nothing (should never happen but if it does it's a bad sign1) 
  BN_TRIG_SW,    //!< triggered by software (force trigger)  
  BN_TRIG_RF,    //!< triggered by input wavecforms
  BN_TRIG_EXT    //!< triggered by external trigger 
} beacon_trig_type_t;


typedef enum beacon_trigger_polarization
{
 H = 0,
 V = 1
} beacon_trigger_polarization_t;

#define BEACON_DEFAULT_TRIGGER_POLARIZATION H

// get the name of the beacon_trigger_polarization_t, returns NULL if not valid
const char* beacon_trigger_polarization_name(beacon_trigger_polarization_t pol);


/** in memory layout of beacon event headers. 
 *
 * STILL PRELIMINARY 
 *
 * On-disk layout is different and opaque, you must use beacon_header_read() / beacon_header_write() to write
 * to disk 
 *
 * I refrained from reducing the number of bits for various things because they will presumably get compressed away anyway. 
 *
 * If not, we can do that in the conversion to the on-disk format. 
 *
 */
typedef struct beacon_header 
{
  uint64_t event_number;                              //!< A unique identifier for this event. If only one board, will match readout number. Otherwise, might skip if the boards are out of sync. 
  uint64_t trig_number;                               //!< the sequential (since reset) trigger number assigned to this event. 
  uint16_t buffer_length;                             //!< the buffer length. Stored both here and in the event. 
  uint16_t pretrigger_samples;                        //!< Number of samples that are pretrigger
  ARRAY1D(uint32_t, readout_time, BN_MAX_BOARDS);     //!< CPU time of readout, seconds
  ARRAY1D(uint32_t, readout_time_ns, BN_MAX_BOARDS);  //!< CPU time of readout, nanoseconds 
  ARRAY1D(uint64_t, trig_time, BN_MAX_BOARDS);        //!< Board trigger time (raw units) 
  uint32_t approx_trigger_time;                       //!< Board trigger time converted to real units (approx secs), master only
  uint32_t approx_trigger_time_nsecs;                 //!< Board trigger time converted to real units (approx nnsecs), master only
  uint32_t triggered_beams;                           //!< The beams that triggered 
  uint32_t beam_mask;                                 //!< The enabled beams
  uint32_t beam_power;                                //!< The power in the triggered beam
  ARRAY1D(uint32_t, deadtime, BN_MAX_BOARDS);         //!< ??? Will we have this available? If so, this will be a fraction. (store for slave board as well) 
  uint8_t buffer_number;                              //!< the buffer number (do we need this?) 
  uint8_t channel_mask;                               //!< The channels allowed to participate in the trigger
  ARRAY1D(uint8_t, channel_read_mask, BN_MAX_BOARDS); //!< The channels actually read
  uint8_t gate_flag;                                  //!< gate flag  (used to be channel_overflow but that was never used) 
  uint8_t buffer_mask;                                //!< The buffer mask at time of read out (do we want this?)   
  ARRAY1D(uint8_t, board_id, BN_MAX_BOARDS);          //!< The board number assigned at startup. If board_id[1] == 0, no slave. 
  beacon_trig_type_t trig_type;                      //!< The trigger type?
  beacon_trigger_polarization_t trig_pol;            //!< The trigger polarization
  uint8_t calpulser;                                  //!< Was the calpulser on? 
  uint8_t sync_problem;                               //!< Various sync problems. TODO convert to enum 
  uint32_t pps_counter;                               //!< value of the pps timer at the time of the event
  uint32_t dynamic_beam_mask;                         //!< the automatic beam masker 
} beacon_header_t; 

/**beacon event body.
 * Holds waveforms. Note that although the buffer length may vary, in memory
 * we always hold max_buffer_size (2048) . Memory is cheap right. Even on the beaglebone, 16KB is no big deal?  (at least, cheaper than dynamic allocation, maybe?) 
 *
 */ 
typedef struct beacon_event
{
  uint64_t event_number;  //!< The event number. Should match event header.  
  uint16_t buffer_length; //!< The buffer length that is actually filled. Also available in event header. 
  ARRAY1D(uint8_t, board_id, BN_MAX_BOARDS);     //!< The board number assigned at startup. If the second board_id is zero, that indicates there is no slave device. 
  ARRAY3D(uint8_t, data,BN_MAX_BOARDS,BN_NUM_CHAN,BN_MAX_WAVEFORM_LENGTH); //!< The waveform data. Only the first buffer_length bytes of each are important. The second array is only filled if there is a slave-device.
} beacon_event_t; 



typedef enum beacon_scaler_type
{
  SCALER_SLOW, 
  SCALER_SLOW_GATED,
  SCALER_FAST
} beacon_scaler_type_t; 

#define BN_SCALER_TIME(type) (type==SCALER_FAST ? 1 : 10) 

/** beacon status. 
 * Holds scalers, deadtime, and maybe some other things 
 **/
typedef struct beacon_status
{
  ARRAY1D(uint16_t, global_scalers, BN_NUM_SCALERS);
  ARRAY2D(uint16_t, beam_scalers, BN_NUM_SCALERS, BN_NUM_BEAMS); //!< The scaler for each beam (12 bits) 
  uint32_t deadtime;                                             //!< The deadtime fraction (units tbd) 
  uint32_t readout_time;                                         //!< CPU time of readout, seconds
  uint32_t readout_time_ns;                                      //!< CPU time of readout, nanoseconds 
  ARRAY1D(uint32_t, trigger_thresholds, BN_NUM_BEAMS);           //!< The trigger thresholds    
  uint64_t latched_pps_time;                                     //!< A timestamp corresponding to a pps time 
  uint8_t board_id;                                              //!< The board number assigned at startup. 
  uint32_t dynamic_beam_mask;                                    //!<  the dynamic beam mask 
} beacon_status_t; 




/* Power state of FPGA (the board can be on but the FPGA off) */ 
typedef enum beacon_gpio_power_state
{
  BN_FPGA_POWER_MASTER = 1, 
  BN_SPI_ENABLE = 2
    //TODO: figure out more of these

} beacon_gpio_power_state_t; 

//TODO: define these corectly
#define GPIO_FPGA_ALL 0x3 
#define GPIO_ALL 0x3


//TODO figure out what else needs to be here
typedef struct beacon_hk
{
  uint32_t unixTime; 
  uint16_t unixTimeMillisecs; 
  int8_t temp_board;  //C, or -128 if off
  int8_t temp_adc;
  // int8_t temp_adc_0; // rename to temp_adc
  // int8_t temp_adc_1; // no longer here

  //TODO: these are in mA... make sure that it's actually enough bits! Or change units if necessary. 
  uint16_t frontend_current; 
  uint16_t adc_current; 
  uint16_t aux_current; 
  uint16_t ant_current; 

  beacon_gpio_power_state_t gpio_state; 
  uint32_t disk_space_kB; 
  uint32_t free_mem_kB;  
} beacon_hk_t; 


/** print the status  prettily */
int beacon_status_print(FILE *f, const beacon_status_t * st) ; 

/** print the header  prettily */
int beacon_header_print(FILE *f, const beacon_header_t * h) ; 

/** print the event prettily. The separator character will be used to separate different fields so you can dump it into a spreadsheet or something */
int beacon_event_print(FILE *f, const beacon_event_t * ev, char sep) ; 

/** Print the HK status pretilly */ 
int beacon_hk_print(FILE * f, const beacon_hk_t * hk); 

/** write this header to file. The size will be different than sizeof(beacon_header_t). Returns 0 on success. */
int beacon_header_write(FILE * f, const beacon_header_t * h); 

/** write this header to compressed file. The size will be different than sizeof(beacon_header_t). Returns 0 on success. */
int beacon_header_gzwrite(gzFile f, const beacon_header_t * h); 

/** read this header from file. The size will be different than sizeof(beacon_header_t). Returns 0 on success. */ 
int beacon_header_read(FILE * f, beacon_header_t * h); 

/** read this header from compressed file. The size will be different than sizeof(beacon_header_t). Returns 0 on success. */ 
int beacon_header_gzread(gzFile  f, beacon_header_t * h); 

/** Write the event body to a file. Returns 0 on success. The number of bytes written is not sizeof(beacon_event_t). */ 
int beacon_event_write(FILE * f, const beacon_event_t * ev); 

/** Read the event body from a file. Returns 0 on success. The number of bytes read is not sizeof(beacon_event_t). */ 
int beacon_event_read(FILE * f, beacon_event_t * ev); 

/** Write the event body to a compressed file. Returns 0 on success. The number of bytes written is not sizeof(beacon_event_t). */ 
int beacon_event_gzwrite(gzFile f, const beacon_event_t * ev); 

/** Read the event body from a compressed file. Returns 0 on success. The number of bytes read is not sizeof(beacon_event_t). */ 
int beacon_event_gzread(gzFile f, beacon_event_t * ev); 

/** Write the status to a file. Returns 0 on success. The number of bytes written is not sizeof(beacon_status_t). */ 
int beacon_status_write(FILE * f, const beacon_status_t * ev); 

/** Read the statusbody from a file. Returns 0 on success. The number of bytes read is not sizeof(beacon_status_t). */ 
int beacon_status_read(FILE * f, beacon_status_t * ev); 

/** Write the status to a compressed file. Returns 0 on success. The number of bytes written is not sizeof(beacon_status_t). */ 
int beacon_status_gzwrite(gzFile f, const beacon_status_t * ev); 

/** Read the status from a compressed file. Returns 0 on success. The number of bytes read is not sizeof(beacon_status_t). */ 
int beacon_status_gzread(gzFile f, beacon_status_t * ev); 

/** write this hk to file. The size will be different than sizeof(beacon_hk_t). Returns 0 on success. */
int beacon_hk_write(FILE * f, const beacon_hk_t * h); 

/** write this hk to compressed file. The size will be different than sizeof(beacon_hk_t). Returns 0 on success. */
int beacon_hk_gzwrite(gzFile f, const beacon_hk_t * h); 

/** read this hk from file. The size will be different than sizeof(beacon_hk_t). Returns 0 on success. */ 
int beacon_hk_read(FILE * f, beacon_hk_t * h); 

/** read this hk from compressed file. The size will be different than sizeof(beacon_hk_t). Returns 0 on success. */ 
int beacon_hk_gzread(gzFile  f, beacon_hk_t * h); 

#undef ARRAY1D
#undef ARRAY2D
#undef ARRAY3D

#ifdef __cplusplus
}
#endif
#endif
