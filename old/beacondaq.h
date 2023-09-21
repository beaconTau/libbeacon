#ifndef _beacondaq_h
#define _beacondaq_h

#include "beacon.h" 

/** \file beacondaq.h  
 *
 * Include file for talking to the hardware. 
 *
 * Cosmin Deaconu <cozzyd@kicp.uhicago.edu> 
 *
 * This header defines structures, constants, and functions
 * for working with the phased array hardware. 
 *
 *
 */


/** Number of chunks in an address */ 
#define BN_NUM_CHUNK 4 

/** Number of bytes in a word */
#define BN_WORD_SIZE 4 



/** opaque handle for device */ 
struct beacon_dev; 
/** typedef for device */ 
typedef struct beacon_dev beacon_dev_t; 

typedef uint8_t beacon_buffer_mask_t; 

typedef struct beacon_trigger_enable
{
  uint8_t enable_beamforming : 1;
  uint8_t enable_beam8  : 1;
  uint8_t enable_beam4a  : 1;
  uint8_t enable_beam4b  : 1;
} beacon_trigger_enable_t; 


typedef struct beacon_trigger_output_config
{
  uint8_t enable : 1; 
  uint8_t polarity : 1; 
  uint8_t send_1Hz : 1; 
  uint8_t width; 
} beacon_trigger_output_config_t ; 


typedef struct beacon_ext_input_config 
{
  uint8_t use_as_trigger : 1;  //otherwise just as gate for scalers
  uint16_t trig_delay;         //if used as trigger, delay  is 128 ns * this 
} beacon_ext_input_config_t; 

typedef enum beacon_which_board
{
  MASTER = 0, 
  SLAVE  =1
} beacon_which_board_t; 




/** Firmware info retrieved from board */ 
typedef struct beacon_fwinfo
{
  struct 
  {
    unsigned major : 4; 
    unsigned minor : 4; 
    unsigned master: 1; 
  } ver; 

  struct
  {
    unsigned year : 12; 
    unsigned month : 4; 
    unsigned day : 5; 
  } date;

  uint64_t dna;  //!< board dna 
} beacon_fwinfo_t; 


typedef enum beacon_reset_type 
{
  BN_RESET_COUNTERS, //!< resets event counter / trig number / trig time only 
  BN_RESET_CALIBRATE,      //!< recalibrates ADC if necessary  
  BN_RESET_ALMOST_GLOBAL, //!< everything but register settings 
  BN_RESET_GLOBAL    //! everything 
} beacon_reset_t; 



typedef struct beacon_veto_options
{
  uint8_t veto_pulse_width;    
  uint8_t saturation_cut_value;
  uint8_t cw_cut_value;
  uint8_t extended_cut_value;
  uint8_t sideswipe_cut_value;
  uint8_t enable_saturation_cut  : 1; 
  uint8_t enable_cw_cut          : 1; 
  uint8_t enable_sideswipe_cut    : 1; 
  uint8_t enable_extended_cut    : 1;
 
} beacon_veto_options_t; 


/** \brief Open a beacon phased array board and initializes it. 
 *
 * This opens a beacon phased array board and returns a pointer to the opaque
 * device handle. 
 *
 *
 * If that turns out to be too slow, I guess we can write a kernel driver. 
 *
 * Optionally, a mutex can be created to help synchronize access to this device
 * from multiple threads. 
 *
 * For now, the device handle also keeps track of the board id, buffer length
 * and the readout number offset. On initialization, the board id is set to the next
 * available id, buffer length is set to the default amount (624 samples) and
 * the readout number is set to unixtime << 32. They can be set to something
 * better using beacon_set_board_id, beacon_set_buffer_length and beacon_set_readout_number_offset.
 *
 *
 * The access to the SPI file descriptor is locked when opening, so only one
 * process can hold it. 
 *
 *
 * @param spi_master_device_name The master SPI device (likely something like /dev/spidev2.0) 
 * @param spi_slave_device_name The slave SPI device (likely something like /dev/spidev1.0) , or 0 for single board mode
 * @param power_gpio_number If positive, the GPIO that controls the board (and should be enabled at start) 
 * @param thread_safe  If non-zero a mutex will be initialized that will control concurrent access 
 *                     to this device from multiple threads.
 *
 *
 * @returns a pointer to the file descriptor, or 0 if something went wrong. 
 */
beacon_dev_t * beacon_open(const char * spi_master_device_name, 
                             const char * spi_slave_device_name, 
                             int power_gpio_number, 
                             int thread_safe) ; 

/** Deinitialize the phased array device and frees all memory. Do not attempt to use the device after closing. */ 
int beacon_close(beacon_dev_t * d); 

/**Set the board id for the device. Note that slave will be number +1. */
void beacon_set_board_id(beacon_dev_t * d, uint8_t number, beacon_which_board_t which_board) ;


/** Set the readout number offset. Currently DOES NOT reset the counter
 * on the board (only done on beacon_open / beacon_reset). 
 *
 * This means you must run this either right after open or reset and before reading any buffers
 * for you to get something sensible.  
 * @param d board handle
 * @param offset the offset to set
 *
 **/ 
void beacon_set_readout_number_offset(beacon_dev_t * d, uint64_t offset); 


/** Sends a board reset. The reset type is specified by type. 
 *
 * @param d the board to reset
 * @param type The type of reset to do. See the documentation for beacon_reset_t 
 * After reset, the phased trigger will be disabled and will need to be enabled if desired. 
 * @returns 0 on success
 */
int beacon_reset(beacon_dev_t *d, beacon_reset_t type); 

/**Retrieve the board id for the current event. */
uint8_t beacon_get_board_id(const beacon_dev_t * d, beacon_which_board_t which_board) ; 


/** Set the length of the readout buffer. Can be anything between 0 and 2048. (default is 624). */ 
void beacon_set_buffer_length(beacon_dev_t *d, uint16_t buffer); 

/** Retrieves the current buffer length */ 
uint16_t beacon_get_buffer_length(const beacon_dev_t *d); 


/** Send a software trigger to the device
 * @param d the device to send a trigger to. 
 *
 **/ 
int beacon_sw_trigger(beacon_dev_t * d); 

/** Change the state of the calpulser */ 
int beacon_calpulse(beacon_dev_t * d, unsigned state) ; 

/** Waits for data to be available, or time out, or beacon_cancel_wait. 
 * 
 * Will busy poll beacon_check_buffers (which) 
 *
 * If ready is passed, it will be filled after done waiting. Normally it should
 * be non-zero unless interrupted or the timeout is reached. 
 *
 * A timeout may be passed in seconds if you don't want to wait forever (and who wouldn't?) 
 *
 * The "correct way" to interrupt this by using beacon_cancel_wait (either
 * from a signal handler or another thread). 
 *
 * If interrupted, (normally by beacon_cancel_wait,), will return EINTR and ready (if passed) will be set to 0. 
 *
 * We also immediately return EAGAIN if there is a previous call to beacon_cancel_wait that didn't actually cancel anything (like
 * if it was called when nothing was waiting). 
 *
 * Right now only one thread is allowed to wait at a time. If you try waiting from another
 * thread, it will return EBUSY. This is only enforced if the device has locks enabled.  
 *
 * Returns 0 on success,  
 * 
 **/
int beacon_wait(beacon_dev_t *d, beacon_buffer_mask_t * ready, float timeout_seconds, beacon_which_board_t which); 

/** Checks to see which buffers are ready to be read
 * If next_buffer is non-zero, will fill it with what the board things the next buffer to read is. 
 * */ 
beacon_buffer_mask_t beacon_check_buffers(beacon_dev_t *d, uint8_t*  next_buffer, beacon_which_board_t which);

/** Retrieve the firmware info */
int beacon_fwinfo(beacon_dev_t *d, beacon_fwinfo_t* fwinfo, beacon_which_board_t which); 



/** Fills in the status struct. 
 **/ 
int beacon_read_status(beacon_dev_t *d, beacon_status_t * stat, beacon_which_board_t which); 

/**
 * Highest level read function. This will wait for data, read it into the 
 * required number of events, clear the buffer, and increment the event number appropriately 
 *
 * You must pass it a pointer to an array of the maximum number of buffers e.g. 
 
  \verbatim
  int nread; 
  beacon_header_t headers[BN_NUM_BUFFER]; 
  beacon_event_t events[BN_NUM_BUFFER]; 
  nread = wait_for_and_read_multiple_events(device, &headers, &events); 
  \endverbatim
 
 * OR the dynamically allocated variant
 
  \verbatim
  int nread; 
  beacon_header_t (*headers)[BN_NUM_BUFFER] = malloc(sizeof(*headers)); 
  beacon_header_t (*events)[BN_NUM_BUFFER] = malloc(sizeof(*headers)); 
  nread = wait_for_and_read_multiple_events(device, &headers, &events); 
 
  \endverbatim
 *
 *
 * Returns the number of events read. 
 *
 **/ 
int beacon_wait_for_and_read_multiple_events(beacon_dev_t * d, 
                                              beacon_header_t (*headers_master)[BN_NUM_BUFFER], 
                                              beacon_event_t  (*events_master)[BN_NUM_BUFFER]
                                              
                                              ) ; 


/** Read a single event, filling header and event, and also clearing the buffer and increment event number. Does not check if there is anyting available in the buffer.  
 *
 * @param d the device handle
 * @param buffer the buffer to read
 * @param header header to write to 
 * @param event event to write to 
 * Returns 0 on success.
 * */ 
int beacon_read_single(beacon_dev_t *d, uint8_t buffer, 
                        beacon_header_t * header, beacon_event_t * event
                        );


/** Reads buffers specified by mask. An event and header  must exist for each
 * buffer in the array pointed to by header_arr and event_arr ( Clears each buffer after reading and increments event numbers
 * appropriately).  Returns 0 on success. 
 *
 **/
int beacon_read_multiple_array(beacon_dev_t *d, beacon_buffer_mask_t mask, 
                                beacon_header_t *header_arr,  beacon_event_t * event_arr
                                ); 
 
/** Reads buffers specified by mask. An pointer to event and header  must exist for each
 * buffer in the array pointed to by header_arr and event_arr ( Clears each
 * buffer after reading and increments event numbers appropriately).  Returns 0
 * on success. 
 **/
int beacon_read_multiple_ptr(beacon_dev_t *d, beacon_buffer_mask_t mask, 
                              beacon_header_t **header_ptr_arr,  beacon_event_t ** event_ptr_arr
                              ); 



/** Lowest-level waveform read command. 
 * Read the given addresses from the buffer and channel and put into data (which should be the right size). 
 * Does not clear the buffer or increment event number. 
 *
 **/ 
int beacon_read_raw(beacon_dev_t *d, uint8_t buffer, uint8_t channel, uint8_t start_ram, uint8_t end_ram, uint8_t * data, beacon_which_board_t which); 


/** Lowest-level write command. Writes 4 bytes from buffer to device (if master/slave, to both) */ 
int beacon_write(beacon_dev_t *d, const uint8_t* buffer); 

/** Lowest-level read command. Reads 4 bytes into buffer */ 
int beacon_read(beacon_dev_t *d, uint8_t* buffer, beacon_which_board_t which); 

/** Clear the specified buffers. Returns 0 on success. */ 
int beacon_clear_buffer(beacon_dev_t *d, beacon_buffer_mask_t mask); 

/** This cancels the current beacon_wait. If there
 * is no beacon_wait, it will prevent the first  future one from running
 * Should be safe to call this from a signal handler (hopefully :). 
 */
void beacon_cancel_wait(beacon_dev_t *d) ; 

/** 
 * low level register read 
 *
 * @param d device handle
 * @param address The register address to read
 * @param result  where to store the result, should be 4 bytes. 
 * @param slave   if 1, read from slave instead of master (or single board) 
 * @return 0 on success
 * 
 **/ 
int beacon_read_register(beacon_dev_t * d, uint8_t address, uint8_t * result, beacon_which_board_t which); 

/** Set the spi clock rate in MHz (default 10MHz)*/ 
int beacon_set_spi_clock(beacon_dev_t *d, unsigned clock); 

/** toggle chipselect between each transfer (Default yes) */ 
int beacon_set_toggle_chipselect(beacon_dev_t *d, int cs_toggle); 

/** toggle additional delay between transfers (Default 0) */ 
int beacon_set_transaction_delay(beacon_dev_t *d, unsigned delay_usecs); 



/** Set all the thresholds 
 * @param trigger_thresholds array of thresholds, should have BN_NUM_BEAMS members
 * @param dont_set_mask mask of beams not to set (pass 0 to set all). 
 */
int beacon_set_thresholds(beacon_dev_t* d, const uint32_t *trigger_thresholds, uint32_t dont_set_mask); 

/** Get all the thresholds
 * @param trigger_thresholds array of thresholds to be filled. Should have BN_NUM_BEAMS members.
 */
int beacon_get_thresholds(beacon_dev_t* d, uint32_t *trigger_thresolds); 

/** Set the trigger mask. 1 to use all beams */ 
int beacon_set_trigger_mask(beacon_dev_t* d, uint32_t mask); 

/* Get the trigger mask. */ 
uint32_t beacon_get_trigger_mask(beacon_dev_t* d); 


/** Set the attenuation for each channel .
 *  Should have BN_NUM_CHAN members for both master and slave. If 0, not applied for that board. 
 */ 
int beacon_set_attenuation(beacon_dev_t * d, const uint8_t * attenuation_master, const uint8_t * attenuation_slave); 

/** Get the attenuation for each channel .
 *  Should have BN_NUM_CHAN members. If 0, not read. 
 */ 
int beacon_get_attenuation(beacon_dev_t * d, uint8_t * attenuation_master, uint8_t  *attenuation_slave); 


/** Sets the channels used to form the trigger.
 * */ 
int beacon_set_channel_mask(beacon_dev_t * d, uint8_t channel_mask); 

/** Gets the channels used to form the trigger.
 * 8 LSB's for master, then for slave .
 * */ 
uint16_t beacon_get_channel_mask(beacon_dev_t *d); 

/** Set the trigger enables */ 
int beacon_set_trigger_enables(beacon_dev_t *d, beacon_trigger_enable_t enable, beacon_which_board_t which); 

/** Get the trigger enables */ 
beacon_trigger_enable_t beacon_get_trigger_enables(beacon_dev_t *d, beacon_which_board_t which); 

/** Enable or disable phased array readout. */ 
int beacon_phased_trigger_readout(beacon_dev_t *d, int enable);

/** Set the trigger holdoff (in the appropriate units) */ 
int beacon_set_trigger_holdoff(beacon_dev_t *d, uint16_t holdoff); 

/** Get the trigger holdoff */ 
uint16_t beacon_get_trigger_holdoff(beacon_dev_t *d); 

/** Set the pretrigger (0-7). Does it for both boards*/ 
int beacon_set_pretrigger(beacon_dev_t *d, uint8_t pretrigger); 

/** Get the pretrigger (0-7) (should be the same for both boards).
 *
 * This just returns the cached value, so it's possible that it was changed from underneath us. 
 * */ 
uint8_t beacon_get_pretrigger(const beacon_dev_t *d); 

/** Set the external output config */ 
int beacon_configure_trigger_output(beacon_dev_t * d, beacon_trigger_output_config_t config); 

/** get the external output config */ 
int beacon_get_trigger_output(beacon_dev_t * d, beacon_trigger_output_config_t * config); 

/** Set the external triger config */ 
int beacon_configure_ext_trigger_in(beacon_dev_t * d, beacon_ext_input_config_t config); 

/** get the external trigger config */ 
int beacon_get_ext_trigger_in(beacon_dev_t * d, beacon_ext_input_config_t * config); 

int beacon_enable_verification_mode(beacon_dev_t * d, int mode); 

/* 0 if not on, 1 if on, -1 if error */ 
int beacon_query_verification_mode(beacon_dev_t * d); 

/** The poll interval for waiting, in us. If 0, will just do a sched_yield */ 
int beacon_set_poll_interval(beacon_dev_t *, unsigned short us); 

/** Sets the trigger delays. Should have BN_NUM_CHAN members */ 
int beacon_set_trigger_delays(beacon_dev_t *d, const uint8_t * delays); 

/** Gets the trigger delays. Should have BN_NUM_CHAN members */ 
int beacon_get_trigger_delays(beacon_dev_t *d, uint8_t * delays); 

// /** Set the minimum threshold for any beam (default, 5000) */ 
// int beacon_set_min_threshold(beacon_dev_t *d, uint32_t min_threshold); 

/* Set the trigger polarization */
int beacon_set_trigger_polarization(beacon_dev_t* d, beacon_trigger_polarization_t pol);

/* Get the trigger polarization */
beacon_trigger_polarization_t beacon_get_trigger_polarization(beacon_dev_t* d);


/** Set the trigger-path low pass filter */ 
int beacon_set_trigger_path_low_pass(beacon_dev_t *d, int on); 

/** get the trigger_path low pass, 0 = off, 1 = on, otherwise some error */ 
int beacon_get_trigger_path_low_pass(beacon_dev_t *d); 

/** Set the dynamic masking options */ 
int beacon_set_dynamic_masking(beacon_dev_t *d, int enable, uint8_t threshold, uint16_t holdoff); 

/** retrieve the dynamic masking options */ 
int beacon_get_dynamic_masking(beacon_dev_t *d, int * enable, uint8_t * threshold, uint16_t * holdoff); 


/**  Set the veto options */
int beacon_set_veto_options(beacon_dev_t * d, const beacon_veto_options_t * opt); 

/**  Retrieve the veto options */
int beacon_get_veto_options(beacon_dev_t * d, beacon_veto_options_t * opt); 




#endif
