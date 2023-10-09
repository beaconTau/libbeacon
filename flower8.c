#include "flower8.h" 
#include <linux/spi/spidev.h> 
#include <unistd.h> 
#include <errno.h>
#include <assert.h>
#include <sys/ioctl.h> 
#include <pthread.h>
#include <sys/file.h> 
#include <poll.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> 
#include <fcntl.h> 
#include <endian.h>
#include <sys/types.h> 
#include <time.h>
#include <math.h>

//#define SAFE
#define GOLDILOCKS
//#define PARALLEL_READOUT

typedef enum
{
  FLWR8_REG_FW_VER = 0x01,
  FLWR8_REG_FW_DATE = 0x02,
  FLWR8_REG_SCAL_RD = 0x03, 
  FLWR8_REG_DATA_STATUS = 0x07, 
  FLWR8_REG_EVT_COUNTER = 0x0a, 
  FLWR8_REG_TRG_COUNTER = 0x0b, 
  FLWR8_REG_TRG_PPS = 0x0c, 
  FLWR8_REG_TRG_TIMELO = 0x0d, 
  FLWR8_REG_TRG_TIMEHI = 0x0e, 
  FLWR8_REG_TRG_INFO = 0x0f, 
  FLWR8_REG_TRG_CHANNELS = 0x10, 
  FLWR8_REG_I2C_READ = 0x22, 
  FLWR8_REG_DATA_CHUNK0 = 0x23,
  FLWR8_REG_DATA_CHUNK1 = 0x24,
  FLWR8_REG_DATA_CHUNK2 = 0x25,
  FLWR8_REG_DATA_CHUNK3 = 0x26,
  FLWR8_REG_SCAL_UPD = 0x28,
  FLWR8_REG_SCAL_SEL = 0x29, 
  FLWR8_REG_CALPULSE=0x2a, 
  FLWR8_REG_SCAL_TIME_LOW = 0x2c, 
  FLWR8_REG_SCAL_TIME_HIGH = 0x2d, 
  FLWR8_REG_SCAL_SPEED_SELECT = 0x2f, 
  FLWR8_REG_PD_REG=0x3a,
  FLWR8_REG_CFG_REG0 = 0x3b, 
  FLWR8_REG_CFG_REG1 = 0x3c, 
  FLWR8_REG_TRIG_ENABLES = 0x3d, 
  FLWR8_REG_FORCE_TRIG = 0x40,
  FLWR8_REG_CHANNEL = 0x41,
  FLWR8_REG_RAM_DEPTH = 0x44, 
  FLWR8_REG_RAM_ADDR = 0x45,
  FLWR8_REG_READ = 0x47,
  FLWR8_REG_CLR_READOUT = 0x48,
  FLWR8_REG_PRETRIG= 0x4C,
  FLWR8_REG_BUF_CLEAR= 0x4D,
  FLWR8_REG_TRIG_CH0_THR = 0x56,
  FLWR8_REG_TRIG_CH1_THR = 0x57,
  FLWR8_REG_TRIG_CH2_THR = 0x58,
  FLWR8_REG_TRIG_CH3_THR = 0x59,
  FLWR8_REG_TRIG_CH4_THR = 0x5a,
  FLWR8_REG_TRIG_CH5_THR = 0x5b,
  FLWR8_REG_TRIG_CH6_THR = 0x5c,
  FLWR8_REG_TRIG_CH7_THR = 0x5d,
  FLWR8_REG_PPS_DELAY = 0x5e, 
  FLWR8_REG_COINCTRIG_SETUP = 0x5f,
  FLWR8_REG_SYSTRIG = 0x60,
  FLWR8_REG_SMATRIG = 0x61,
  FLWR8_REG_TRIG_MASK = 0x62, 
  FLWR8_REG_SYNC = 0x63,
  FLWR8_REG_SET_READ_REG = 0x6d,
  FLWR8_REG_RESET_COUNTERS=0x7e,
  FLWR8_REG_MAX=0x7f
} e_flower8_reg; 

typedef enum
{
  HMCAD_ADR_QUAD_CGAIN = 0x2A, 
  HMCAD_ADR_DUAL_CGAIN = 0x2B, 
  HMCAD_ADR_CGAIN_CFG = 0x33 
} e_hmcad_reg; 

#define USING(d) if (d->enable_locking)  { pthread_mutex_lock(&d->lock);}
#define DONE(d)  if (d->enable_locking) {pthread_mutex_unlock(&d->lock);}


struct flower8_dev
{
  int spi_fd; 
  FILE *spi_enable_file; 
  int spi_en_gpio; 
  int interrupt_fd; 
  int enable_locking; 
  pthread_mutex_t lock; //lock for spi bus 
#ifdef PARALLEL_READOUT
  volatile int alive; 
  pthread_t acq_work_thread;
  struct acq_work
  {
    int nsamps; 
    uint8_t ** dest; 
    struct timespec start_time;
    struct timespec end_time;
  } work; 
  pthread_cond_t work_ready; 
  pthread_mutex_t work_mutex; 
#endif
  int flags; 
  struct pollfd interrupt_fdset; 
  union
  {
    struct 
    {
      uint8_t addr;
      uint8_t major;
      uint8_t reserved; 
      uint8_t rev : 4; 
      uint8_t minor : 4; 
    } ver; 
    flower8_word_t word; 
  } fwver; 
  int fwver_int;

  union
  {
    struct 
    {
      uint16_t year; 
      uint8_t day; 
      uint8_t month; 
    } date; 
    flower8_word_t word; 
  } fwdate;


#ifdef FOOLISH
  uint32_t readout_tx_scratch[1024]; 
  uint32_t readout_rx_scratch[1024]; 
  int8_t readout_rx_dest[1024]; 
#endif
#ifdef GOLDILOCKS
  int32_t readout_rx_scratch[1024]; 
  int8_t readout_rx_dest[1024]; 
#endif
};

#ifdef PARALLEL_READOUT
void * acq_worker(void* d)
{
  flower8_dev_t * dev = (flower8_dev_t *) d ; 

  pthread_mutex_lock(&dev->work_mutex); 
  while (dev->alive)
  {
    pthread_cond_wait(&dev->work_ready, &dev->work_mutex); 
    if (!dev->alive)
    {
      break; 
    }

    //otherwise, we are ready to do an acquisition! 
    clock_gettime(CLOCK_MONOTONIC, &dev->work.start_time); 
    flower8_read_waveforms(dev, dev->work.nsamps, dev->work.dest);
    clock_gettime(CLOCK_MONOTONIC, &dev->work.end_time); 
  }

  pthread_mutex_unlock(&dev->work_mutex); 
  return NULL; 
}

#endif


struct flower8_bouquet
{
  flower8_dev_t * M; 
  flower8_dev_t * S; 
  flower8_trigger_config_t trig_cfg; 
  uint8_t trig_thresh[8]; 
  uint8_t servo_thresh[8]; 
  uint16_t trigger_mask; 
  uint16_t buflen; 
  uint64_t event_number_offset; 
  flower8_variable_scaler_type_t scal_speed; 
}; 



void flower8_set_event_number_offset(flower8_bouquet_t *b, uint64_t ofst) 
{

  b->event_number_offset = ofst; 
}

static int export_gpio_if_not_exported(int gpionum) 
{
  char buf[128]; 
  sprintf(buf, "/sys/class/gpio/gpio%d", gpionum); 
  if (access(buf, F_OK))
  {
        FILE * fexport = fopen("/sys/class/gpio/export","w");
        assert(fexport); 
        fprintf(fexport,"%d\n",gpionum);
        fclose(fexport); 
        usleep(100000); //wait to make sure it come up 
        return access(buf, F_OK); 
  }
      return 0; 
}

static int spi_msg(int fd, int nmsg, struct spi_ioc_transfer * msgs)
{  
//   for(int i = 0; i < nmsg;i++) assert(msgs[i].tx_buf != 0 || msgs[i].rx_buf !=0); 	  
   return ioctl(fd, SPI_IOC_MESSAGE(nmsg), msgs); 
}

static int write_words(flower8_dev_t *dev, int N,  const flower8_word_t * words) 
{
  if (!dev) return -1; 
  USING(dev); 
  int ret =  ((int) (N*sizeof(*words))) != write(dev->spi_fd, words, N*sizeof(*words)); 
  DONE(dev); 
  return ret; 
}
static int write_word(flower8_dev_t *dev, const flower8_word_t * word) 
{

  if (!dev) return -1; 
  USING(dev); 
  int ret =  ((int)sizeof(*word)) != write(dev->spi_fd, word, sizeof(*word)); 
  DONE(dev); 
  return ret; 
}

static int write_word_unlocked(flower8_dev_t *dev, const flower8_word_t * word) 
{

  if (!dev) return -1; 
  int ret =  ((int)sizeof(*word)) != write(dev->spi_fd, word, sizeof(*word)); 
  return ret; 
}



flower8_bouquet_t * flower8_bouquet_prepare(flower8_dev_t * M, flower8_dev_t * S)
{
  flower8_bouquet_t * b = 0; 
  if (!M)
  {
    fprintf(stderr,"Bouquet requires at least one master board\n"); 
    return 0; 
  }

  b = calloc(1,sizeof(*b)); 

  b->M = M; 
  b->S = S; 

  //read in the current thresholds 
  flower8_word_t thresh_word; 
  for (int i = 0; i < FLOWER8_MAX_TRIG_CHAN; i++) 
  {
    flower8_read_register(b->M, FLWR8_REG_TRIG_CH0_THR+i, &thresh_word); 
    b->trig_thresh[i] = thresh_word.bytes[3]; 
    b->servo_thresh[i] = thresh_word.bytes[2]; 
  }

  //read in the scaler speed

  flower8_word_t scal_speed_word = {0}; 
  flower8_read_register(b->M, FLWR8_REG_SCAL_SPEED_SELECT, &scal_speed_word); 
  b->scal_speed = scal_speed_word.bytes[3] == 0 ? FLOWER8_SCAL_100mHz : FLOWER8_SCAL_100Hz; 

  //read in the trigger configuration 
  flower8_word_t cfg_word = {0}; 
  flower8_read_register(b->M,FLWR8_REG_COINCTRIG_SETUP, &cfg_word); 
  b->trig_cfg.vpp_mode = cfg_word.bytes[1]; 
  b->trig_cfg.window = cfg_word.bytes[2];  
  b->trig_cfg.num_coinc = cfg_word.bytes[3]; 
 
  if (b->S) 
  {
    //make sure trigger is disabled on S 
    flower8_word_t disable = {.bytes = { FLWR8_REG_TRIG_ENABLES, 0,0,0}} ;
    if (write_word(b->S,&disable))
    {
      fprintf(stderr,"troubling disabling trigger on S\n"); 
      free(b); return 0; 
    }

  }
  // reset counters
  flower8_bouquet_reset(b); 

  flower8_word_t word_mask;  
  flower8_read_register(b->M, FLWR8_REG_TRIG_MASK, &word_mask); 
  b->trigger_mask = word_mask.bytes[3]; 
  b->buflen = 512; 
  return b; 


}

flower8_dev_t * flower8_open(const char * spi_device, int spi_en_gpio, int trig_gpio, int flags) 
{
  flower8_dev_t * dev = 0; 
  int locked_spi, spi_fd; 

  spi_fd = open(spi_device, O_RDWR);
  if (spi_fd < 0) 
  {
    fprintf(stderr,"flower8_open: Could not open %s\n", spi_device); 
    return 0; 
  }

  locked_spi = flock(spi_fd, LOCK_EX | LOCK_NB); 
  if (locked_spi < 0) 
  {
    fprintf(stderr,"flower8_open: could not get exclusive access to %s\n", spi_device); 
    close(spi_fd); 
    return 0; 
  }

  dev = calloc(sizeof(*dev),1); 
  if (!dev) 
  {
    fprintf(stderr,"Could not allocate memory for flower\n"); 
    return 0; 
  }
  dev->spi_fd = spi_fd; 

  dev->flags = flags; 
  if (flags & FLOWER8_ENABLE_LOCKING) 
  {
    dev->enable_locking = 1; 
    pthread_mutex_init(&dev->lock,0); 
  }

  int spi_clock = 12000000; 
  uint8_t mode = 0; 
  uint8_t bits_per_word = 8; 
  ioctl(spi_fd, SPI_IOC_WR_MODE,&mode); 
  ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,&spi_clock); 
  ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD,&bits_per_word); 

  dev->spi_en_gpio = spi_en_gpio; 
  dev->spi_enable_file = 0; 
  if (spi_en_gpio) 
  {
    char buf[512]; 
    int gpionum = abs(spi_en_gpio); 
    export_gpio_if_not_exported(gpionum); 

    //make sure it's NOT active low 
    sprintf(buf,"/sys/class/gpio/gpio%d/active_low",gpionum); 
    FILE * flow = fopen(buf,"w"); 
    fputs("0\n",flow); 
    fclose(flow); 

    //make sure it's an output
    sprintf(buf,"/sys/class/gpio/gpio%d/direction", gpionum); 
    FILE * fdir = fopen(buf,"w"); 
    fputs("out\n",fdir); 
    fclose(fdir); 


    sprintf(buf,"/sys/class/gpio/gpio%d/value", gpionum); 
    dev->spi_enable_file = fopen(buf,"w"); 
    //make sure it has the right value
    fprintf(dev->spi_enable_file,"%d\n", spi_en_gpio < 0 ? 0 : 1); 
  }

  if( trig_gpio) 
  {
    export_gpio_if_not_exported(trig_gpio); 

    char buf[512]; 
    //set the edge to rising 
    sprintf(buf, "/sys/class/gpio/gpio%d/edge", trig_gpio); 
    int edge_fd = open(buf, O_RDWR); 

    if (edge_fd <=0) 
    {
      fprintf(stderr, "Could not open %s for writing, trig gpio won't be set up\n", buf); 
    }
    else
    {
      write(edge_fd,"rising",strlen("rising")); 
      close(edge_fd); 

      sprintf(buf,"/sys/class/gpio/gpio%d/value", trig_gpio); 
      dev->interrupt_fd = open(buf,O_RDWR); 

      if (dev->interrupt_fd > 0) 
      {
        int locked_interrupt = flock(dev->interrupt_fd, LOCK_EX | LOCK_NB);
        if (locked_interrupt) 
        {
          dev->interrupt_fd = 0; 
          fprintf(stderr,"Could not lock interrupt gpio\n"); 
        }
        else
        {
        memset(&dev->interrupt_fdset,0,sizeof(dev->interrupt_fdset)); 
        dev->interrupt_fdset.fd = dev->interrupt_fd; 
        dev->interrupt_fdset.events = POLLPRI; 
        }
      }
      else
      {
        fprintf(stderr, "Could not open %s for writing, trig gpio won't be set up\n", buf); 
      }
    }

  }
  
  flower8_read_register(dev, FLWR8_REG_FW_VER, &dev->fwver.word); 
  flower8_read_register(dev, FLWR8_REG_FW_DATE, &dev->fwdate.word); 
  dev->fwver_int = 10000 * dev->fwver.ver.major + 100 * dev->fwver.ver.minor + dev->fwver.ver.rev; 
  //have to finagle the date
  flower8_word_t word = dev->fwdate.word; 
  dev->fwdate.date.day = word.bytes[3]; 
  dev->fwdate.date.month = word.bytes[2] & 0xf; 
  dev->fwdate.date.year = (((uint32_t)word.bytes[1]) << 4) | (word.bytes[2] >> 4); 

  #ifdef PARALLEL_READOUT
  pthread_mutex_init(&dev->work_mutex,0); 
  pthread_cond_init(&dev->work_ready,0); 
  pthread_create(&dev->acq_work_thread, 0, acq_worker, dev); 
  dev->alive = 1; 
#endif



  return dev; 
}



/*
static int read_word (flower8_dev_t * dev, flower8_word_t *word) 
{
  return ((int)sizeof(*word)) != read(dev->spi_fd, word, sizeof(*word)); 
}
*/

int flower8_read_registers(flower8_dev_t*dev, int nreg,  const uint8_t *  addr, flower8_word_t * results) 
{
  if (!dev) return 0-1; 
#define nregs_at_a_time 24 
  struct spi_ioc_transfer xfer[nregs_at_a_time*2] = {0}; //
  flower8_word_t solicit_words[nregs_at_a_time] = {0}; 

  //todo, see if we can interlace
  unsigned ireg =0; 
  int ret = 0;
  for (int i = 0; i < nreg; i++)
  {
    if (addr[i] <1 || addr[i] > FLWR8_REG_MAX) continue; 

    solicit_words[ireg].bytes[0] = FLWR8_REG_SET_READ_REG; 
    solicit_words[ireg].bytes[3] = addr[i]; 
    xfer[2*ireg].tx_buf = (uintptr_t) solicit_words[ireg].bytes; 
    xfer[2*ireg].len = 4; 
    xfer[2*ireg].rx_buf=0; 
    xfer[2*ireg].cs_change=1; 
    xfer[2*ireg].delay_usecs=0; 
    xfer[2*ireg+1].tx_buf = 0; 
    xfer[2*ireg+1].rx_buf = (uintptr_t) results[i].bytes; 
    xfer[2*ireg+1].len = 4; 
    xfer[2*ireg+1].cs_change=1; 
    xfer[2*ireg+1].delay_usecs=0; 
    ireg++; 

    if (ireg == nregs_at_a_time || i == nreg-1) 
    {
      USING(dev); 
      ret += ( (int) (ireg*sizeof(flower8_word_t)) != spi_msg(dev->spi_fd, 2*ireg, xfer)); 
      DONE(dev); 
      ireg = 0; 
    }
  }
  return ret; 
}

int flower8_read_register(flower8_dev_t*dev, uint8_t addr, flower8_word_t * result) 
{
  return flower8_read_registers(dev,1,&addr,result); 
}

int flower8_set_thresholds(flower8_bouquet_t *b, const uint8_t * trigger_thresholds, const uint8_t * servo_thresholds, uint8_t mask) 
{
  if (!b || !b->M) return -1; 

  flower8_word_t words[FLOWER8_MAX_TRIG_CHAN] = {0}; 

  int ii = 0; 
  for (int i = 0; i < FLOWER8_MAX_TRIG_CHAN; i++) 
  {
    if (mask & (1 << i)) 
    {
      uint8_t servo = servo_thresholds[i]; 
      uint8_t trig = trigger_thresholds[i]; 
      if (servo > 127) servo = 127; 
      if (trig > 127) trig = 127; 
      words[ii].bytes[0] = FLWR8_REG_TRIG_CH0_THR+i; 
      words[ii].bytes[2] = servo; 
      words[ii].bytes[3] = trig; 
      b->trig_thresh[i] = trig;
      b->servo_thresh[i] = servo;
      ii++; 
    }
  }
  int ret =  write_words(b->M, ii, words); 
  return ret; 
}

int flower8_configure_trigger(flower8_bouquet_t * b, flower8_trigger_config_t  cfg) 
{
  if (!b || !b->M) return -1; 
  int ret = 0;
  flower8_word_t word = {0}; 
  word.bytes[0] = FLWR8_REG_COINCTRIG_SETUP;
  word.bytes[1] = cfg.vpp_mode; 
  word.bytes[2] = cfg.window; 
  word.bytes[3] = cfg.num_coinc; 
  ret = write_word(b->M,&word); 
  if (!ret) b->trig_cfg = cfg; 
  return ret; 
}

int flower8_bouquet_discard(flower8_bouquet_t *b, int trash)
{
  if (trash) 
  {
    if (b->M) flower8_close(b->M); 
    if (b->S) flower8_close(b->S); 
  }

  free(b); 
  return 0; 

}

int flower8_close(flower8_dev_t * dev)
{
  if (!dev) return -1; 

#ifdef PARALLEL_READOUT
  dev->alive = 0; 
  pthread_mutex_lock(&dev->work_mutex); 
  pthread_cond_signal(&dev->work_ready); 
  pthread_mutex_unlock(&dev->work_mutex); 
  pthread_join(dev->acq_work_thread,NULL); 
#endif


  flock(dev->spi_fd, LOCK_UN); 
  close(dev->spi_fd); 
  if (dev->spi_enable_file) 
  {
    fprintf(dev->spi_enable_file,"%d\n", dev->spi_en_gpio < 0 ? 1 : 0); 
    fclose(dev->spi_enable_file); 
  }

  free(dev); 
  return 0; 
}


static flower8_word_t scal_sel_regs[34]; 
__attribute__((constructor)) 
static void fill_scal_sel_regs() 
{
  for (int i = 0; i < 34; i++) 
  {
    scal_sel_regs[i].bytes[0] = FLWR8_REG_SCAL_SEL;
    scal_sel_regs[i].bytes[3] = i; 
  }
}

int flower8_fill_daqstatus(flower8_bouquet_t *b, flower8_daqstatus_t *ds)
{

  if (!b || !b->M) return -1; 


  #define MAX_DSNMSG (3*(30)+5)

  struct spi_ioc_transfer xfer[MAX_DSNMSG] = {0}; 

  static flower8_word_t update_word = {.bytes = {FLWR8_REG_SCAL_UPD,0,0,1}}; 
  static flower8_word_t selectread_word = {.bytes = {FLWR8_REG_SET_READ_REG,0,0, FLWR8_REG_SCAL_RD}};
  static flower8_word_t update_tlow = {.bytes = {FLWR8_REG_SET_READ_REG,0,0,FLWR8_REG_SCAL_TIME_LOW}}; 
  static flower8_word_t update_thigh = {.bytes = {FLWR8_REG_SET_READ_REG,0,0,FLWR8_REG_SCAL_TIME_HIGH}}; 
  flower8_word_t dest_scaler[34] = {0}; 
  uint16_t raw_scalers[64]; // this will be ocpied from dest_scaler
  flower8_word_t dest_time[2] = {0}; 

  struct timespec start;
  struct timespec end;
  

  for (int i = 0; i < FLOWER8_MAX_TRIG_CHAN; i++) 
  {
    ds->trig_thresholds[i] = b->trig_thresh[i];
    ds->servo_thresholds[i] = b->servo_thresh[i];
  }

  //TODO can speed this up by interlacing reads and writes and caching the first part
  
  xfer[0].tx_buf = (uintptr_t) update_word.bytes; 
  xfer[0].len = sizeof(flower8_word_t); 
  xfer[0].cs_change = 1; 

  xfer[1].tx_buf  = (uintptr_t) update_tlow.bytes; 
  xfer[1].len = sizeof(flower8_word_t); 
  xfer[1].cs_change = 1; 
  xfer[2].rx_buf  = (uintptr_t) dest_time[0].bytes;
  xfer[2].len = sizeof(flower8_word_t); 
  xfer[2].cs_change = 1; 

  xfer[3].tx_buf  =  (uintptr_t)update_thigh.bytes; 
  xfer[3].len = sizeof(flower8_word_t); 
  xfer[3].cs_change = 1; 
  xfer[4].rx_buf  = (uintptr_t) dest_time[1].bytes;
  xfer[4].len = sizeof(flower8_word_t); 
  xfer[4].cs_change = 1; 


  int ixfer = 0; 
  int max_reg = 34; 
  for (int ireg = 0; ireg <max_reg; ireg++) 
  {
	  if (ireg == 9) ireg++;  
	  if (ireg == 19) ireg++;  
    if (ireg == 29) ireg=31; //scalers 58-61 are empty
    xfer[3*ixfer+5].tx_buf = (uintptr_t) scal_sel_regs[ireg].bytes; 
    xfer[3*ixfer+5].len = sizeof(flower8_word_t);
    xfer[3*ixfer+5].rx_buf = 0;
    xfer[3*ixfer+5].cs_change=1; 
    xfer[3*ixfer+6].tx_buf =  (uintptr_t)selectread_word.bytes; 
    xfer[3*ixfer+6].rx_buf = 0;
    xfer[3*ixfer+6].cs_change=1; 
    xfer[3*ixfer+6].len = sizeof(flower8_word_t);
    xfer[3*ixfer+7].rx_buf =  (uintptr_t )dest_scaler[ireg].bytes; // will have to finagle these after
    xfer[3*ixfer+7].len = sizeof(flower8_word_t);
    xfer[3*ixfer+7].tx_buf =  0; 
    xfer[3*ixfer+7].cs_change=1; 
    ixfer++; 
  }

  int nxfer =  3*ixfer+5; 

  clock_gettime(CLOCK_REALTIME,&start);
  USING(b->M); 
  int ret = spi_msg(b->M->spi_fd, nxfer, xfer); 
  DONE(b->M); 
  clock_gettime(CLOCK_REALTIME,&end);
//  printf("status ioctl: %d\n", ret); 

  ds->when = (start.tv_sec*0.5 + end.tv_sec*0.5) + 1e-9*(start.tv_nsec*0.5 + end.tv_nsec*0.5); 
  ds->scaler_speed =b->scal_speed;

  if (ret > 0) 
  {
    for (int i = 0; i < 32; i++) 
    {
      uint16_t low =  dest_scaler[i].bytes[3] | ((dest_scaler[i].bytes[2] & 0x0f ) << 8) ;
      uint16_t high = (dest_scaler[i].bytes[1] << 4)  | ((dest_scaler[i].bytes[2] & 0xf0)>>4); 
      raw_scalers[2*i] = low;
      raw_scalers[2*i+1] = high;
    }

    ds->s_1Hz.trig_coinc = raw_scalers[0];
    for (int i = 0; i < 8; i++) ds->s_1Hz.trig_per_chan[i] = raw_scalers[1+i]; 
    ds->s_1Hz.servo_coinc = raw_scalers[9];
    for (int i = 0; i < 8; i++) ds->s_1Hz.servo_per_chan[i] = raw_scalers[10+i]; 
    ds->s_1Hz_gated.trig_coinc = raw_scalers[20];
    for (int i = 0; i < 8; i++) ds->s_1Hz_gated.trig_per_chan[i] = raw_scalers[21+i]; 
    ds->s_1Hz_gated.servo_coinc = raw_scalers[29];
    for (int i = 0; i < 8; i++) ds->s_1Hz_gated.servo_per_chan[i] = raw_scalers[30+i]; 
    ds->s_100mHz.trig_coinc = raw_scalers[40];
    for (int i = 0; i < 8; i++) ds->s_100mHz.trig_per_chan[i] = raw_scalers[41+i]; 
    ds->s_100mHz.servo_coinc = raw_scalers[49];
    for (int i = 0; i < 8; i++) ds->s_100mHz.servo_per_chan[i] = raw_scalers[50+i]; 
    

    uint64_t t_low = ( be32toh(dest_time[0].word) & 0xffffff ); 
    uint64_t t_high = ( be32toh(dest_time[1].word) & 0xffffff ); 
    ds->ncycles =  t_low | t_high << 24; 
    ds->scaler_counter_1Hz = raw_scalers[63]; 
    uint64_t cyc_low =( be32toh(dest_scaler[32].word) & 0xffffff);  
    uint64_t cyc_high =( be32toh(dest_scaler[33].word) & 0xffffff);  
    ds->cycle_counter = cyc_low  | (cyc_high << 24); 

    return 0; 
  }

  return -1; 

}

int flower8_dump(FILE * f, flower8_dev_t *dev) 
{
  int ret = 0; 
  ret+= fprintf(f,"FLOWER HANDLE at 0x%p\n", dev); 
  if (!dev) 
  {
    fprintf(f,"  NULL HANDLE!!!\n"); 
    return -1; 
  }
  ret+= fprintf(f,"  FWVER:  %02d.%02d.%02d (0x%x, [0x%x,0x%x,0x%x,0x%x])\n", 
                dev->fwver.ver.major, dev->fwver.ver.minor, dev->fwver.ver.rev, 
                dev->fwver.word.word, 
                dev->fwver.word.bytes[0], dev->fwver.word.bytes[1], 
                dev->fwver.word.bytes[2], dev->fwver.word.bytes[3]); 
  ret+= fprintf(f,"  FWDATE:  %d-%02d-%02d (0x%x, [0x%x,0x%x,0x%x,0x%x])\n", 
                dev->fwdate.date.year, dev->fwdate.date.month, dev->fwdate.date.day, 
                dev->fwdate.word.word,
                dev->fwdate.word.bytes[0], dev->fwdate.word.bytes[1], 
                dev->fwdate.word.bytes[2], dev->fwdate.word.bytes[3]); 
  return ret; 
}


int flower8_bouquet_dump(FILE * f, flower8_bouquet_t * b) 
{
  int ret = 0; 
  ret += fprintf(f,"BOUQUET AT 0x%p,  M=0x%p, S=0x%p\n", b, b->M,b->S); 
  if (b->M) 
  {
    ret += fprintf(f,"M:  "); 
    ret +=flower8_dump(f, b->M); 
  }
  if (b->S)
  {
    ret += fprintf(f,"S:  "); 
    ret += flower8_dump(f, b->S); 
  }
  ret+= fprintf(f,"  TRIGCONFIG:  window: %d, num_coinc: %d, vpp_mode: %d\n", 
                b->trig_cfg.window, b->trig_cfg.num_coinc, b->trig_cfg.vpp_mode); 

  for (int i = 0; i < 4; i++) 
  {
     ret+= fprintf(f,"  THRESH_CH%d:  servo:  %d, trig: %d\n", i, b->servo_thresh[i], b->trig_thresh[i]);
  }
 
  return ret; 
}

//static flower8_word_t sw_trig_low = {.bytes={FLWR8_REG_FORCE_TRIG,0,0,0}}; 
static flower8_word_t sw_trig = {.bytes={FLWR8_REG_FORCE_TRIG,0,0,1}}; 
static flower8_word_t sync_S = {.bytes={FLWR8_REG_SYNC,0,0,2}}; 
static flower8_word_t sync_M = {.bytes={FLWR8_REG_SYNC,0,0,1}}; 
static flower8_word_t sync_N = {.bytes={FLWR8_REG_SYNC,0,0,0}}; 

int flower8_force_trigger(flower8_bouquet_t * b) 
{
  if (!b || !b->M) return -1; 
  int ret = 0; 
  USING(b->S); 
  USING(b->M); 
  ret+= write_word_unlocked(b->S, &sync_S); 
  ret+= write_word_unlocked(b->M, &sync_M); 
  ret+= write_word_unlocked(b->M, &sw_trig); 
  ret+= write_word_unlocked(b->S, &sw_trig); 
  ret+= write_word_unlocked(b->M, &sync_N); 
  ret+= write_word_unlocked(b->S, &sync_N); 
  DONE(b->S); 
  DONE(b->M); 
  return ret; 
}

static flower8_word_t buffer_clear = {.bytes={FLWR8_REG_BUF_CLEAR,0,0,1}}; 
int flower8_buffer_clear(flower8_bouquet_t * b) 
{
  if (b->S)  
  {
    USING(b->S); 
    USING(b->M); 
    int ret = 0; 
    ret+= write_word_unlocked(b->S, &buffer_clear); 
    ret+= write_word_unlocked(b->M, &buffer_clear);
    DONE(b->S); 
    DONE(b->M); 
    return ret; 
  }
  else return write_word(b->M, &buffer_clear); 
}

static int flower8_buffer_check(flower8_dev_t * dev, int * avail) 
{
  flower8_word_t check = {0}; 
  int ret = flower8_read_register(dev, FLWR8_REG_DATA_STATUS, &check); 
  if (avail)  *avail = check.bytes[3] & 0x1; 
  return ret;
}

int flower8_event_poll(flower8_bouquet_t * b, int * avail) 
{
  flower8_word_t check; 
  int ret = flower8_read_register(b->S ?: b->M, FLWR8_REG_DATA_STATUS, &check); 
  if (!ret && avail)  *avail = check.bytes[3] & 0x1; 
  return ret; 
}


flower8_dev_t * flower8_bouquet_get_M(flower8_bouquet_t *b) 
{
  return b->M; 
}

flower8_dev_t * flower8_bouquet_get_S(flower8_bouquet_t *b) 
{
  return b->S; 
}

static int do_poll(flower8_dev_t * bd, int timeout) 
{

  if (!bd) return -1; 
  char val; 
  lseek(bd->interrupt_fd,0,SEEK_SET); 
  read(bd->interrupt_fd, &val,1); 
  if (val=='1') return 1; 
  int rc= poll(&bd->interrupt_fdset,1,timeout); 

  if (rc && (bd->interrupt_fdset.revents & POLLPRI))
  {
    lseek(bd->interrupt_fd,0,SEEK_SET); 
    read(bd->interrupt_fd, &val,1); 
    return val=='1'; 
  }
  //note that POLLPRI | POLLERR seems to be the defalut response, 
  //so checking this AFTER pollpri in case there's actually an error? 
  if (bd->interrupt_fdset.revents & POLLERR) 
  {
    return errno; 
  }

  if (rc == 0) return 0; 

  return -1; 
}

int flower8_event_wait(flower8_bouquet_t * b, int timeout) 
{
  if (!b) return -1; 
  if (!b->M) return -1; 
  if(!b->M->interrupt_fd) return EIO; 
  return do_poll(b->M, timeout); 
}


//TODO 
//this talks to both boards synchronously even though in principle both are independent!!! 
int flower8_read_waveforms(flower8_dev_t *dev, int nsamps, uint8_t ** dest)
{

  if (!dev) return -1; 

#ifdef BENCHMARK
  struct timespec start;
  struct timespec stop;
  struct timespec ioctl_start;
  struct timespec ioctl_stop;
  double ioctl_time = 0; 
  int nioctl = 0; 
  int nxfers = 0; 
  clock_gettime(CLOCK_REALTIME,&start);
#endif

#define NADDR 1024


#ifndef GOLDILOCKS
  static flower8_word_t select_data[2]  =
  { {.bytes={FLWR8_REG_DATA_CHUNK0, 0,0,0}}
  , {.bytes={FLWR8_REG_DATA_CHUNK1, 0,0,0}}
  };

  static flower8_word_t select_chip[2]  =
  { {.bytes={FLWR8_REG_CHANNEL, 0,0,1}}
  , {.bytes={FLWR8_REG_CHANNEL, 0,0,2}} };



  static flower8_word_t select_addr[NADDR] = {0}; 

  if (!select_addr[0].bytes[0])
  {
      for (int i = 0; i < NADDR; i++) 
      {
        select_addr[i].bytes[0] =FLWR8_REG_RAM_ADDR;
        select_addr[i].bytes[3] =i;
      }
  }
#else
  static flower8_word_t select_chunk1[3]  = {{0}, {0}, {.bytes={FLWR8_REG_DATA_CHUNK1, 0,0,0}}};
  static flower8_word_t select_addr[NADDR][3] = {0}; 
  static flower8_word_t select_chip[2][3]  =
  { 
    { {.bytes={FLWR8_REG_CHANNEL, 0,0,1}}, {.bytes={FLWR8_REG_RAM_ADDR,0,0,0}}, {.bytes={FLWR8_REG_DATA_CHUNK0,0,0,0}}},
    { {.bytes={FLWR8_REG_CHANNEL, 0,0,2}}, {.bytes={FLWR8_REG_RAM_ADDR,0,0,0}}, {.bytes={FLWR8_REG_DATA_CHUNK0,0,0,0}}}
  };




  if (!select_addr[0][1].bytes[0])
  {
      for (int i = 0; i < NADDR; i++) 
      {
        select_addr[i][1].bytes[0] =FLWR8_REG_RAM_ADDR;
        select_addr[i][1].bytes[3] =i;
        select_addr[i][2].bytes[0] =FLWR8_REG_DATA_CHUNK0;
      }
  }
#endif

  if (nsamps > NADDR * 2) nsamps = NADDR * 2; 

  int ret = 0; 
  ///////////////////////////////////////////////////
#ifdef SAFE
#define SLICE_SIZE 32
  struct spi_ioc_transfer xfer[10*SLICE_SIZE+1] = {0}; 
  for (int ichip = 0; ichip < 2; ichip++) 
  {

    //TODO: this can be optimized for partial readouts... 
    //
      int isamp = 0; 
      while (isamp < nsamps) 
      {
        int xfer_counter = 0; 
        xfer[xfer_counter].tx_buf = (uintptr_t) select_chip[ichip].bytes; 
        xfer[xfer_counter].rx_buf =0; 
#define XFER(delay) \
        xfer[xfer_counter].len =4; \
        xfer[xfer_counter].speed_hz =12000000; \
        xfer[xfer_counter].delay_usecs =delay; \
        xfer[xfer_counter++].cs_change =1; 
        XFER(0)
        
        //put half the data in one channel, the other half in the other, then interlace afterwards
        for (int islice = 0; islice < SLICE_SIZE; islice++)
        {
          xfer[xfer_counter].tx_buf = (uintptr_t) select_addr[isamp/2].bytes; 
          xfer[xfer_counter].rx_buf = 0;
          XFER(0)
          xfer[xfer_counter].tx_buf = (uintptr_t) select_data[0].bytes;
          xfer[xfer_counter].rx_buf = 0; 
          XFER(0)
          xfer[xfer_counter].tx_buf =0;
          xfer[xfer_counter].rx_buf = (uintptr_t) &dest[4*ichip+1][isamp]; 
          XFER(0)
          xfer[xfer_counter].tx_buf = (uintptr_t) select_data[1].bytes;
          xfer[xfer_counter].rx_buf = 0; 
          XFER(0)
          xfer[xfer_counter].tx_buf =0;
          xfer[xfer_counter].rx_buf = (uintptr_t) &dest[4*ichip + 3][isamp]; 
          XFER(0)
          xfer[xfer_counter].tx_buf = (uintptr_t) select_addr[isamp/2+1].bytes; 
          xfer[xfer_counter].rx_buf = 0;
          XFER(0)
          xfer[xfer_counter].tx_buf = (uintptr_t) select_data[0].bytes;
          xfer[xfer_counter].rx_buf = 0; 
          xfer[xfer_counter].delay_usecs = 100; 
          XFER(0)
          xfer[xfer_counter].tx_buf =0;
          xfer[xfer_counter].rx_buf = (uintptr_t) &dest[4*ichip][isamp]; 
          XFER(0)
          xfer[xfer_counter].tx_buf = (uintptr_t) select_data[1].bytes;
          xfer[xfer_counter].rx_buf = 0; 
          XFER(0)
          xfer[xfer_counter].tx_buf =0;
          xfer[xfer_counter].rx_buf = (uintptr_t) &dest[4*ichip+2][isamp]; 
          XFER(0)
 
          isamp+=4; 
          if(isamp >= nsamps) break; 
        }

#ifdef BENCHMARK
	clock_gettime(CLOCK_REALTIME, &ioctl_start);
#endif
        USING(dev); 
        spi_msg(dev->spi_fd, xfer_counter, xfer); 
        DONE(dev); 
#ifdef BENCHMARK
	clock_gettime(CLOCK_REALTIME, &ioctl_stop);
	ioctl_time += ioctl_stop.tv_sec - ioctl_start.tv_sec + 1e-9 * (ioctl_stop.tv_nsec - ioctl_start.tv_nsec);
	nioctl++; 
	nxfers += xfer_counter; 
#endif
      }

     //deinterlace 
     uint8_t tmp[2]; 
     for (int isamp =0; isamp < nsamps; isamp+=4)
     {
       for(int chunk = 0; chunk < 2; chunk++) 
       {
       //TODO: rewrite using ARM intrinsics 
          memcpy(tmp, &dest[4*ichip+2*chunk+1][isamp+2], 2); 
          memcpy(&dest[4*ichip+2*chunk+1][isamp+2], &dest[4*ichip+2*chunk][isamp],2);
          memcpy( &dest[4*ichip+2*chunk][isamp], tmp, 2);

       }
     }

  }

  //method with scratch buffer
#endif//safe

  ///////////////////////////////////////////////////
#ifdef GOLDILOCKS
  int channel_i[8] = {0}; 
#define SLICE_SIZE 64 
  struct spi_ioc_transfer xfer[4*SLICE_SIZE+1] = {0}; 
  for (int ichip = 0; ichip < 2; ichip++) 
  {
    int isamp = 0; 
    int rx_i = 0;
    while (isamp < nsamps) 
    {
      int rx_dest = 2*ichip; 
      int xfer_counter = 0; 
#define XFER(tx,rx,length) \
        xfer[xfer_counter].tx_buf = (uintptr_t) (tx ); \
        xfer[xfer_counter].rx_buf = (uintptr_t) ( (rx) ? (dev->readout_rx_scratch + rx_i): 0) ; \
        xfer[xfer_counter].len =  length ; \
        if (rx) \
        {\
          dev->readout_rx_dest[rx_i++] = rx_dest; \
          for (int iii =0; iii < length/4-1; iii++) dev->readout_rx_dest[rx_i++] = -1; \
          rx_dest = 2 * ichip + (rx_dest+1)%2; \
        }  \
        xfer[xfer_counter].cs_change =1; \
        xfer[xfer_counter++].delay_usecs=0;

     for (int islice = 0; islice < SLICE_SIZE; islice++) 
     {
       //if this is the first iteration, we start with a select_chip, otherwise it's a readout
       XFER( islice == 0 ? select_chip[ichip][0].bytes : select_addr[isamp/2][0].bytes,
             islice > 0, 12); 
       XFER( select_chunk1, 1, 12); 
       XFER ( select_addr[isamp/2+1][0].bytes, 1, 12); 
       XFER(select_chunk1,1,12);
       isamp+= 4; 
       if (isamp >= nsamps) break; 
     }

     //we have one more read, actually! 
     XFER(0,1,12); 
#ifdef BENCHMARK
    	clock_gettime(CLOCK_REALTIME, &ioctl_start);
#endif
      USING(dev); 
      spi_msg(dev->spi_fd, xfer_counter, xfer); 
      DONE(dev); 
#ifdef BENCHMARK
      clock_gettime(CLOCK_REALTIME, &ioctl_stop);
      ioctl_time += ioctl_stop.tv_sec - ioctl_start.tv_sec + 1e-9 * (ioctl_stop.tv_nsec - ioctl_start.tv_nsec);
      nioctl++; 
      nxfers += xfer_counter;
#endif
      for (int i = 0; i < rx_i; i++) 
      {
        if (dev->readout_rx_dest[i] >=0)
        {
          int chpair = dev->readout_rx_dest[i]; 
          flower8_word_t w = {.word = dev->readout_rx_scratch[i]}; 
          dest[2*chpair][channel_i[2*chpair]++] = w.bytes[0]; 
          dest[2*chpair][channel_i[2*chpair]++] = w.bytes[1]; 
          dest[2*chpair+1][channel_i[2*chpair+1]++] = w.bytes[2]; 
          dest[2*chpair+1][channel_i[2*chpair+1]++] = w.bytes[3]; 
        }
      }
      rx_i = 0;
 
    }
  }
#endif
  ///////////////////////////////////////////////////
#ifdef FOOLISH
  int scratch_i = 0; 
  int channel_i[8] = {0}; 

#define APPEND_XFER(tx,dest)\
  dev->readout_tx_scratch[scratch_i] = tx;\
  dev->readout_rx_dest[scratch_i++] = dest;\
//  dev->readout_tx_scratch[scratch_i] = 0;  dev->readout_rx_dest[scratch_i++] = -1;

  struct spi_ioc_transfer xfer = 
  { 
     .tx_buf = (uintptr_t) dev->readout_tx_scratch, 
     .rx_buf = (uintptr_t) dev->readout_rx_scratch,
     .speed_hz = 1500000,
  }; 

  for (int ichip = 0; ichip < 2; ichip++)
  {

    int isamp = 0; 
    while (isamp < nsamps)
    {

      APPEND_XFER(select_chip[ichip].word,-1);

      for (int islice = 0; islice < 32; islice++)
      {
         APPEND_XFER(select_addr[isamp/2].word,-1);
         APPEND_XFER(select_data[0].word,-1);
         APPEND_XFER(0,2*ichip);
         APPEND_XFER(select_data[1].word,-1);
         APPEND_XFER(0,2*ichip);
         APPEND_XFER(select_addr[isamp/2+1].word,-1);
         APPEND_XFER(select_data[0].word,-1);
         APPEND_XFER(0,2*ichip + 1);
         APPEND_XFER(select_data[1].word,-1);
         APPEND_XFER(0,2*ichip + 1);
         isamp+=4; 
         if(isamp >= nsamps) break; 
      }


      xfer.len = 4 * scratch_i; 
#ifdef BENCHMARK
    	clock_gettime(CLOCK_REALTIME, &ioctl_start);
#endif
      USING(dev); 
      spi_msg(dev->spi_fd, 1, &xfer); 
      DONE(dev); 
#ifdef BENCHMARK
      clock_gettime(CLOCK_REALTIME, &ioctl_stop);
      ioctl_time += ioctl_stop.tv_sec - ioctl_start.tv_sec + 1e-9 * (ioctl_stop.tv_nsec - ioctl_start.tv_nsec);
      nioctl++; 
      nxfers++;
#endif
      for (int i = 0; i < scratch_i; i++) 
      {
         if (dev->readout_rx_dest[i] >=0)
         {
            int chpair = dev->readout_rx_dest[i]; 
            flower8_word_t w = {.word = dev->readout_rx_scratch[i]}; 
            dest[2*chpair][channel_i[2*chpair]++] = w.bytes[0]; 
            dest[2*chpair][channel_i[2*chpair]++] = w.bytes[1]; 
            dest[2*chpair+1][channel_i[2*chpair+1]++] = w.bytes[2]; 
            dest[2*chpair+1][channel_i[2*chpair+1]++] = w.bytes[3]; 
         }
      }
      scratch_i = 0;
    }
  }
#endif

#ifdef BENCHMARK
  clock_gettime(CLOCK_REALTIME,&stop);
  double T = stop.tv_sec - start.tv_sec + 1e-9 * (stop.tv_nsec - start.tv_nsec); 
  printf("  reading out %d-sample waveforms took %fs(%f kBps). %f in %d ioctls (%d xfers) (%f%% of total time, %f/%fs per ioctl/xfer). \n", nsamps, T, nsamps * 8 / T / 1024, ioctl_time, nioctl, nxfers,  100*ioctl_time/T, ioctl_time/nioctl, ioctl_time/nxfers); 
#endif
 
  return ret; 
}


int flower8_set_gains(flower8_dev_t *dev, const uint8_t * codes) 
{

  if (!dev) return -1; 
  for (int ichip = 0; ichip < 2; ichip++) 
  {
    flower8_word_t words[3]  = 
    {
      //select chip  
      {.bytes={FLWR8_REG_CFG_REG0,0,0,ichip}} ,
      //make sure in xcfg 
      {.bytes={ FLWR8_REG_CFG_REG1, HMCAD_ADR_CGAIN_CFG,0,1}} ,
      // set gains
      {.bytes=
         { 
            FLWR8_REG_CFG_REG1,
            HMCAD_ADR_QUAD_CGAIN,
            (codes[4*ichip+2] & 0xf)  | ((codes[4*ichip+3] & 0xf) << 4),
            (codes[4*ichip] & 0xf)  | ((codes[4*ichip+1] & 0xf) << 4)
         }
      }
    };
    write_word(dev,&words[0]);
    write_word(dev,&words[1]);
    write_word(dev,&words[2]);
  }
  return 0; 
} 

static double getrms(int N, uint8_t* X) 
{
  double sum = 0; 
  double sum2 = 0; 
  for (int i = 0; i < N ; i++) 
  {
    sum+=X[i]; 
    sum2+=X[i]*X[i]; 
  }

  double mean = sum/N; 
  return sqrt(sum2/N - mean*mean); 
}


int flower8_set_trigger_enables(flower8_bouquet_t *b, flower8_trigger_enables_t enables)

{
  //not sure if extin should be 1 but... let's just do it? 
  flower8_word_t tin = {.bytes = {FLWR8_REG_TRIG_ENABLES,0, enables.enable_coinc, enables.enable_pps }}; 
  flower8_word_t tout = {.bytes={FLWR8_REG_SMATRIG,0,enables.enable_pps,enables.enable_coinc}};
  return write_word(b->M,&tout) || write_word(b->M,&tin); 
}

int flower8_get_trigger_enables(flower8_bouquet_t *b, flower8_trigger_enables_t *enables)
{

  if (!enables) return -1; 
  flower8_word_t word = {0}; 
  flower8_read_register(b->M, FLWR8_REG_TRIG_ENABLES, &word); 
  enables->enable_coinc = word.bytes[2]; 
  enables->enable_pps = word.bytes[3]; 
  return 0; 
}

int flower8_fill_metadata(flower8_bouquet_t *b,flower8_event_metadata_t* meta) 
{

  static uint8_t regs[7] = { FLWR8_REG_EVT_COUNTER, FLWR8_REG_TRG_COUNTER, FLWR8_REG_TRG_PPS, FLWR8_REG_TRG_TIMELO, FLWR8_REG_TRG_TIMEHI, FLWR8_REG_TRG_INFO, FLWR8_REG_TRG_CHANNELS } ; 
  flower8_word_t wM[7] = {0}; 
  flower8_word_t wS[7] = {0}; 
  flower8_read_registers(b->M, 7, regs, wM);
  if (b->S)
  {
    flower8_read_registers(b->S, 7, regs, wS);
    if (wS[0].word != wM[0].word )
    {
      fprintf(stderr, "event# mismatch! [ 0x%x,0x%0x], [0x%0x, 0x%0x]\n", be32toh(wM[0].word), be32toh(wM[1].word), be32toh(wS[0].word), be32toh(wS[1].word));
    }
  }

  meta->event_number = be32toh(wM[0].word) & 0xffffff; 
  meta->trig_number = be32toh(wM[1].word) & 0xffffff; 
  meta->pps_count = be32toh(wM[2].word); 
  meta->timestamp[0] = be32toh(wM[3].word) & 0xffffff; 
  uint64_t big_part =  be32toh(wM[4].word) & 0xffffff;
  meta->timestamp[0] += (big_part << 24); 
  if (b->S)
  {
    meta->timestamp[1] = be32toh(wS[3].word) & 0xffffff; 
    big_part = be32toh(wS[4].word) & 0xffffff; 
    meta->timestamp[1] += (big_part << 24); 

    if ( llabs(meta->timestamp[1] - meta->timestamp[0]) > 10)
    {
      fprintf(stderr, "trigtime mismatch! [ 0x%x,0x%x], [0x%x, 0x%x] diff=%d\n", be32toh(wM[2].word), be32toh(wM[3].word), be32toh(wS[2].word), be32toh(wS[3].word), be32toh(wM[3].word) - be32toh(wS[3].word));
    }
  }
  meta->trig_type = wM[5].bytes[3]  &0xf; 
  meta->pps = wM[5].bytes[2]; 
  meta->trig_channels  = wM[6].bytes[3]; 

  return 0; 
}


int flower8_equalize(flower8_dev_t * dev, float target_rms, uint8_t * v_gain_codes, uint32_t opts)
{
  if (!dev) return -1; 

  float rms[FLOWER8_MAX_TRIG_CHAN] = {0}; 

  uint8_t mask = (~(opts & 0xff)) & 0xff; 
  int verbose = opts & 0x80000000; 
  static uint8_t data[FLOWER8_MAX_TRIG_CHAN][1024]; 
  static uint8_t * data_ptrs[FLOWER8_MAX_TRIG_CHAN] = 
  { data[0], data[1], data[2], data[3], data[4], data[5], data[6],data[7] }; 

  uint8_t gain_codes[FLOWER8_MAX_TRIG_CHAN] = {0}; 
  uint8_t done = 0;

  while (done != mask) 
  {
    flower8_set_gains(dev, gain_codes); 
    write_word(dev,&buffer_clear); 
    write_word(dev,&sw_trig); 
    int avail = 0; 
    while (!avail) flower8_buffer_check(dev,&avail); 

    flower8_read_waveforms(dev, 1024, data_ptrs); 
    write_word(dev,&buffer_clear); 
    for (int i = 0; i < FLOWER8_MAX_TRIG_CHAN; i++) 
    {
      if (done & ( 1 << i) || !(mask & (1 << i))) continue; 

      rms[i] = getrms(256, data[i]); 

      if (verbose) printf("ch: %d, gain_code: %d, rms: %f\n", i, gain_codes[i], rms[i]); 

      if (rms[i] < target_rms && gain_codes[i] < (FLOWER8_GAIN_TOO_HIGH-1)) 
      {
        gain_codes[i]++; 
      }
      else 
      {
        done |= (1 << i); 
        if (v_gain_codes) v_gain_codes[i] = gain_codes[i]; 
        if (verbose) printf("  ch %d done!\n", i); 
      }
    }
  }

  return 0; 
}


int flower8_get_fwversion(flower8_dev_t *dev, uint8_t *major, uint8_t *minor, 
                         uint8_t *rev, uint16_t *year, uint8_t *month, uint8_t *day) 
{

  if (!dev) return -1; 

  if (major) *major = dev->fwver.ver.major; 
  if (minor) *minor = dev->fwver.ver.minor; 
  if (rev) *rev = dev->fwver.ver.rev; 
  if (year) *year = dev->fwdate.date.year; 
  if (month) *month = dev->fwdate.date.month; 
  if (day) *day = dev->fwdate.date.day; 
  return 0; 

}

int flower8_set_delayed_pps_delay(flower8_dev_t * dev, uint32_t delay) 
{
  if (!dev || (dev->fwver_int < 8)) return -1; 

  flower8_word_t word = {.bytes = {FLWR8_REG_PPS_DELAY, (delay >> 16) & 0xff,(delay >> 8) & 0xff,  delay & 0xff, }}; 
  return write_word(dev,&word); 
}

int flower8_get_delayed_pps_delay(flower8_dev_t * dev, uint32_t *delay) 
{
  if (!dev || (dev->fwver_int < 8)) return -1; 
  flower8_word_t word; 
  int ret = flower8_read_register(dev, FLWR8_REG_PPS_DELAY, &word); 
  if (!ret)  *delay = word.bytes[3] | (word.bytes[2] <<8) | (word.bytes[1] << 16); 
  return ret; 

}

int flower8_bouquet_reset(flower8_bouquet_t * b) 
{
  
  //disable all triggers
  flower8_trigger_enables_t enable = {0} ; 
  if (flower8_set_trigger_enables(b,enable))
  {
    fprintf(stderr,"Couldn't disable trigger enables in reset\n"); 
    return -1; 
  }


  if (b->S) 
  {
    //disable trigout for S 
    flower8_word_t souts = { .bytes={FLWR8_REG_SMATRIG, 0,0,0}}; 
    write_word(b->S, &souts); 
  }
 
  //clear buffers in case there are any left
  flower8_buffer_clear(b); 

  if (b->S) 
  {
    //only enable external trigger for S 
    flower8_word_t senables = { .bytes={FLWR8_REG_TRIG_ENABLES, 1,0,0}}; 
    write_word(b->S, &senables); 
  }

  // synchronize 
  if (b->S) 
  {
    if (write_word(b->S,&sync_S)) 
    {
      fprintf(stderr,"stage 0 sync error!!!"); 
      free(b); 
      return 0; 
    }

    if (write_word(b->M,&sync_M)) 
    {
      fprintf(stderr,"stage 1 sync error!!!"); 
      free(b); 
      return 0; 
    }

  }
    
 //reset counters
  flower8_word_t reset_word = { .bytes = { FLWR8_REG_RESET_COUNTERS,0,0,1}}; 

  if (write_word(b->M, &reset_word) || (b->S && write_word(b->S,&reset_word)))
  {
    fprintf(stderr,"Couldn't reset counters\n"); 
    return -1; 
  }


  if (b->S)
  {

    if (write_word(b->M,&sync_N) || write_word(b->S,&sync_N))
    {
      fprintf(stderr,"stage 2 sync error!!!"); 
      free(b); 
      return 0; 
    }
  }

  //discard a force trigger? 

//  flower8_force_trigger(b); 
  flower8_buffer_clear(b); 

  return 0; 
}


int flower8_set_trigger_mask(flower8_bouquet_t *b, uint8_t trig_mask) 
{
  flower8_word_t mask_word = {.bytes={FLWR8_REG_TRIG_MASK,0,0,trig_mask}}; 
  if (!write_word(b->M,&mask_word))
  {
    b->trigger_mask = trig_mask; 
    return 0; 
  }
  return -1; 
}

int flower8_set_pretrigger(flower8_bouquet_t *b, uint8_t pretrig) 
{
  if (pretrig > 10) pretrig = 10; 

  flower8_word_t word = { .bytes = {FLWR8_REG_PRETRIG, 0, 0,  pretrig & 0xf}}; 
  return write_word(b->M, &word) || (b->S && write_word(b->S,&word)); 
}
void flower8_set_buffer_length(flower8_bouquet_t * b, uint16_t len)
{
  if (len > 4096) len = 4096; 
  b->buflen = len; 
}


int flower8_set_variable_scaler_speed(flower8_bouquet_t * b, flower8_variable_scaler_type_t scal)
{
  if (!b || !b->M) return -1; 
  if (b->scal_speed == scal) return 0; 
  
  flower8_word_t scal_spd = {.bytes = { FLWR8_REG_SCAL_SPEED_SELECT, 0, 0, scal == FLOWER8_SCAL_100mHz ? 0 : 1}}; 

  if (write_word(b->M,&scal_spd))
  {
    b->scal_speed = scal; 
    return 0; 
  }

  return -1; 
}

#ifdef _BEACON_
int beacon_wait_for_and_fill_event(flower8_bouquet_t * b, beacon_header_t *hd, beacon_event_t * ev, int timeout) 
{

  int ret = flower8_event_wait(b,timeout); 
  if (ret!=1) return -1; 

  flower8_event_metadata_t meta = {0}; 
#ifndef PARALLEL_READOUT
  struct timespec now; 
#endif
  flower8_fill_metadata(b,&meta); 
//  printf("%x %d\n", meta.trig_type, meta.pps);

  memset(hd,0,sizeof(*hd));
  hd->event_number = meta.event_number + b->event_number_offset;
  hd->trig_number = meta.trig_number + b->event_number_offset;
  hd->buffer_length = b->buflen;
  hd->trig_time[0] = meta.timestamp[0]; 
  hd->trig_time[1] = meta.timestamp[1]; 
  hd->trig_pol = POL_MIXED; 
  hd->channel_mask = b->trigger_mask; 
  hd->trig_type = meta.trig_type == 1 ? BN_TRIG_SW : 
	          meta.trig_type == 2 ? BN_TRIG_EXT : 
		  meta.trig_type == 3 ? BN_TRIG_COINC : 
		  meta.trig_type == 4 ? BN_TRIG_PHASED : 
		  meta.trig_type == 5 ? BN_TRIG_PPS : 
		  BN_TRIG_NONE; 

  hd->gate_flag = meta.pps; 
  hd->coinc_trigger_mask = meta.trig_channels; 
  hd->pps_counter = meta.pps_count; 
  ev->event_number = meta.event_number + b->event_number_offset; 
  ev->buffer_length = b->buflen; 
  ev->board_id[0] = 1; 
  hd->board_id[0] = 1; 
  if (b->S)
  {
    hd->board_id[1] =2 ; 
    ev->board_id[1] =2 ; 
  }

  uint8_t * destM[8] = {0};
  uint8_t * destS[8] = {0};
  int destcnt = 0;
  for (int ichan = 0; ichan < 8; ichan++)  
  {
    destM[destcnt] = ev->data[0][ichan]; 
    destS[destcnt++] = ev->data[1][ichan]; 
  }
#ifdef PARALLEL_READOUT
  pthread_mutex_lock(&b->M->work_mutex); 
  b->M->work.nsamps = b->buflen; 
  b->M->work.dest = destM; 
  pthread_cond_signal(&b->M->work_ready);
  hd->readout_time[0] = b->M->work.start_time.tv_sec; 
  hd->readout_time_ns[0] = b->M->work.start_time.tv_nsec; 
  pthread_mutex_unlock(&b->M->work_mutex); 
#else
  clock_gettime(CLOCK_REALTIME, &now); 
  flower8_read_waveforms(b->M, b->buflen, destM);
  hd->readout_time[0] = now.tv_sec; 
  hd->readout_time_ns[0] = now.tv_nsec; 
#endif

  if (b->S) 
  {
#ifdef PARALLEL_READOUT
    pthread_mutex_lock(&b->S->work_mutex); 
    b->S->work.nsamps = b->buflen; 
    b->S->work.dest = destS; 
    pthread_cond_signal(&b->S->work_ready);
    hd->readout_time[1] = b->S->work.start_time.tv_sec; 
    hd->readout_time_ns[1] = b->S->work.start_time.tv_nsec; 
    pthread_mutex_unlock(&b->S->work_mutex); 
#else
    clock_gettime(CLOCK_REALTIME, &now); 
    flower8_read_waveforms(b->S, b->buflen, destS);
    hd->readout_time[1] = now.tv_sec; 
    hd->readout_time_ns[1] = now.tv_nsec; 
#endif
  }
 
  flower8_buffer_clear(b); 
  return 0; 
}



int beacon_fill_status(flower8_bouquet_t * b, beacon_status_t *s) 
{

  if (!b || !b->M) return -1; 

  memset(s,0,sizeof(*s));

  flower8_daqstatus_t ds; 
  if (flower8_fill_daqstatus(b,&ds))
  {
    fprintf(stderr,"Problem reading daqstatus?\n"); 
  }

  s->global_scalers[2] = ds.s_1Hz.trig_coinc; 
  s->global_scalers[1] = ds.s_1Hz_gated.trig_coinc; 
  s->global_scalers[0] = ds.s_100mHz.trig_coinc; 
  s->global_servo_scalers[2] = ds.s_1Hz.servo_coinc; 
  s->global_servo_scalers[1] = ds.s_1Hz_gated.servo_coinc; 
  s->global_servo_scalers[0] = ds.s_100mHz.servo_coinc; 

  for (int i = 0; i < BN_NUM_CHAN; i++) 
  {
    s->channel_trig_scalers[i][2] = ds.s_1Hz.trig_per_chan[i]; 
    s->channel_trig_scalers[i][1] = ds.s_1Hz_gated.trig_per_chan[i]; 
    s->channel_trig_scalers[i][0] = ds.s_100mHz.trig_per_chan[i]; 
    s->channel_servo_scalers[i][2] = ds.s_1Hz.servo_per_chan[i]; 
    s->channel_servo_scalers[i][1] = ds.s_1Hz_gated.servo_per_chan[i]; 
    s->channel_servo_scalers[i][0] = ds.s_100mHz.servo_per_chan[i]; 
    s->channel_trig_thresholds[i] = ds.trig_thresholds[i]; 
    s->channel_servo_thresholds[i] = ds.servo_thresholds[i]; 
  }

  s->readout_time = (int) ds.when; 
  s->readout_time_ns = 1e9 * ( ds.when - s->readout_time); 

  s->latched_pps_time = ds.ncycles; 
  s->board_id = 1; 
  s->deadtime = -1; 
  s->latched_pps_count= ds.cycle_counter; 
  s->scaler_update_counter = ds.scaler_counter_1Hz; 
  s->scaler_type = b->scal_speed;

  return 0; 
}

#endif


