#include "nuphase.h" 
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h> 

//these need to be incremented if the structs change incompatibly
//and then generic_*_read must be updated to delegate appropriately. 
#define NUPHASE_HEADER_VERSION 0 
#define NUPHASE_EVENT_VERSION 0 
#define NUPHASE_STATUS_VERSION 0 
#define NUPHASE_HK_VERSION 0 


#define NUPHASE_HEADER_MAGIC 0xbe  
#define NUPHASE_EVENT_MAGIC  0xac 
#define NUPHASE_STATUS_MAGIC 0x04 
#define NUPHASE_HK_MAGIC     0xcc 


//TODO there are apparently much faster versions of these 

static uint16_t stupid_fletcher16_append(int N, const void * vbuf, uint16_t append) 

{
  int i; 
  uint16_t sum1 = append  & 0xff; 
  uint16_t sum2 = append >> 8;; 
  uint8_t * buf = (uint8_t*) vbuf; 

  for (i = 0; i < N; i++)
  {
    sum1 =  (sum1 +buf[i]) % 255; 
    sum2 += (sum1 + sum2) % 255;;
  }

  return sum1 | (sum2 << 8) ; 
}

static uint16_t stupid_fletcher16(int N, const void * buf) 
{
  return stupid_fletcher16_append(N, buf, 0); 
}




/* we'll handle (gz|f)(read|write) the same 
 * with a little silly trickery */ 
struct generic_file
{
  enum { STDIO, ZLIB } type; 
  union 
  {
    FILE * f;
    gzFile gzf;
  } handle; 
}; 

static int generic_read(struct generic_file gf, int n, void * buf) 
{
  switch(gf.type)
  {
    case STDIO: 
      return fread(buf,1,n, gf.handle.f); 
    case ZLIB: 
      return gzread(gf.handle.gzf,buf,n); 
    default:
      return -1; 
  }
}



static int generic_write(struct generic_file gf, int n, const void * buf) 
{
  switch(gf.type)
  {
    case STDIO: 
      return fwrite(buf, 1,n,gf.handle.f); 
    case ZLIB: 
      return gzwrite(gf.handle.gzf,buf,n); 
    default:
      return -1; 
  }
}



struct packet_start
{
  uint8_t magic; 
  uint8_t ver; 
  uint16_t cksum; 
};

// takes care of the odious task of reading in the packet start 
static int packet_start_read( struct generic_file gf, struct packet_start * start, uint8_t expected_magic, uint8_t maximum_version)
{
  int got; 
  got = generic_read(gf, sizeof(start->magic), &start->magic); 
  if (got != sizeof(start->magic)) return NP_ERR_NOT_ENOUGH_BYTES; 

  if (start->magic != expected_magic)
  {
    fprintf(stderr,"Bad magic byte. Expected 0x%x, got 0x%x\n", expected_magic, start->magic); 
    return NP_ERR_WRONG_TYPE; 
  }


  got = generic_read(gf, sizeof(start->ver), &start->ver); 
  if (got != sizeof(start->ver)) 
  {
    fprintf(stderr,"Did not get enough start bytes\n"); 
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  if (start->ver > maximum_version) 
  {
    fprintf(stderr,"Version %d exceeds maximum %d\n", start->ver , maximum_version); 
    return NP_ERR_BAD_VERSION; 
  }

  got = generic_read(gf,sizeof(start->cksum), &start->cksum); 

  if (got != sizeof(start->cksum))
  {
    fprintf(stderr,"Did not get enough checksum bytes\n"); 
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  return 0; 
}




/* Offsets from start of structs for headers */ 
const int nuphase_header_sizes []=  { sizeof(nuphase_header_t) }; 






/* The on-disk format is just packet_start followed by the newest version of the
 * the header struct.  Every time the version changes,if we have data we care about, 
 * we need to increment the version. 
 */

static int nuphase_header_generic_write(struct generic_file gf, const nuphase_header_t *h)
{
  struct packet_start start; 
  int written; 
  start.magic = NUPHASE_HEADER_MAGIC; 
  start.ver = NUPHASE_HEADER_VERSION; 
  start.cksum = stupid_fletcher16(sizeof(nuphase_header_t), h); 

  written = generic_write(gf, sizeof(start), &start); 
  if (written != sizeof(start)) 
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  written = generic_write(gf, sizeof(nuphase_header_t), h); 
  
  if (written != sizeof(nuphase_header_t))
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  return 0; 
}



static int nuphase_header_generic_read(struct generic_file gf, nuphase_header_t *h) 
{
  struct packet_start start; 
  int got; 
  int wanted; 
  uint16_t cksum; 

  got = packet_start_read(gf, &start, NUPHASE_HEADER_MAGIC, NUPHASE_HEADER_VERSION); 
  if (got) return got; 

  switch(start.ver) 
  {
    //add cases here if necessary 
   case NUPHASE_HEADER_VERSION: //this is the most recent header!
      wanted = sizeof(nuphase_header_t); 
      got = generic_read(gf, wanted, h); 
      cksum = stupid_fletcher16(wanted, h); 
      break; 
    default: 
    return NP_ERR_BAD_VERSION; 
  }

  if (wanted!=got)
  {
    fprintf(stderr,"not enough bytes\n"); 
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  if (cksum != start.cksum) 
  {
    fprintf(stderr,"cksum problem\n"); 
    return NP_ERR_CHECKSUM_FAILED; 
  }

  return 0; 
}


/* The on-disk format is just packet_start followed by the newest version of the
 * the event struct. Note that we only write (and compute the checksum for) buffer length bytes for each event. 
 *
 * very time the version changes,if we have data we care about, 
 * we need to increment the version. 
 */

static int nuphase_event_generic_write(struct generic_file gf, const nuphase_event_t *ev)
{
  struct packet_start start; 
  int written; 
  int i,ibd; 
  start.magic = NUPHASE_EVENT_MAGIC; 
  start.ver = NUPHASE_EVENT_VERSION; 

  start.cksum = stupid_fletcher16(sizeof(ev->event_number), &ev->event_number); 
  start.cksum = stupid_fletcher16_append(sizeof(ev->buffer_length), &ev->buffer_length,start.cksum); 
  start.cksum = stupid_fletcher16_append(sizeof(ev->board_id), &ev->board_id, start.cksum); 

  for (ibd = 0; ibd <NP_MAX_BOARDS ; ibd++)
  {
    if (!ev->board_id[ibd]) continue; 
    for (i = 0; i < NP_NUM_CHAN; i++) 
    {
     start.cksum = stupid_fletcher16_append(ev->buffer_length, ev->data[ibd][i], start.cksum); 
    }
  }


  written = generic_write(gf, sizeof(start), &start); 
  if (written != sizeof(start)) 
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  written = generic_write(gf, sizeof(ev->event_number), &ev->event_number); 

  if (written != sizeof(ev->event_number))
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }


  written = generic_write(gf, sizeof(ev->buffer_length), &ev->buffer_length); 
  if (written != sizeof(ev->buffer_length))
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  written = generic_write(gf, sizeof(ev->board_id), &ev->board_id); 

  if (written != sizeof(ev->board_id)) 
  {
      return NP_ERR_NOT_ENOUGH_BYTES; 
  }
 
  for (ibd = 0; ibd < NP_MAX_BOARDS; ibd++)
  {
    if (!ev->board_id[ibd]) continue; 
    for (i = 0; i <NP_NUM_CHAN; i++)
    {
      written = generic_write(gf, ev->buffer_length, &ev->data[ibd][i][0]); 
      if (written != ev->buffer_length) 
      {
        return NP_ERR_NOT_ENOUGH_BYTES; 
      }
    }
  }

  return 0; 
}


static int nuphase_event_generic_read(struct generic_file gf, nuphase_event_t *ev) 
{
  struct packet_start start; 
  int got; 
  int wanted; 
  uint16_t cksum; 
  int i; 

  got = packet_start_read(gf, &start, NUPHASE_EVENT_MAGIC, NUPHASE_EVENT_VERSION); 
  if (got) return got; 


  //add additional cases if necessary for compatibility
  //
  if (start.ver == NUPHASE_EVENT_VERSION) 
  {
      wanted = sizeof(ev->event_number); 
      got = generic_read(gf, wanted, &ev->event_number); 
      if (wanted != got) return NP_ERR_NOT_ENOUGH_BYTES; 
      cksum = stupid_fletcher16(wanted, &ev->event_number); 

      wanted = sizeof(ev->buffer_length); 
      got = generic_read(gf, wanted, &ev->buffer_length); 
      if (wanted != got) return NP_ERR_NOT_ENOUGH_BYTES; 
      cksum = stupid_fletcher16_append(wanted, &ev->buffer_length,cksum); 

      wanted = sizeof(ev->board_id); 
      got = generic_read(gf, wanted, &ev->board_id); 
      if (wanted != got) return NP_ERR_NOT_ENOUGH_BYTES; 
      cksum = stupid_fletcher16_append(wanted, &ev->board_id,cksum); 

      int ibd; 
      for (ibd = 0; ibd <NP_MAX_BOARDS; ibd++)
      {
        if (!ev->board_id[ibd]) 
        {
          memset(ev->data[ibd],0, sizeof(ev->data[ibd])); 
          continue; 
        }

        for (i = 0; i < NP_NUM_CHAN; i++)
        {
          wanted = ev->buffer_length; 
          got = generic_read(gf, wanted, ev->data[ibd][i]); 
          if (wanted != got) return NP_ERR_NOT_ENOUGH_BYTES; 
          cksum = stupid_fletcher16_append(wanted, ev->data[ibd][i], cksum); 

          // zero out the rest of the memory 
          memset(ev->data[ibd][i] + wanted, 0, NP_MAX_WAVEFORM_LENGTH - wanted); 
        }
      }

  }
  
  else
  {
    fprintf(stderr,"Unimplemented version: %d\n", start.ver); 
    return NP_ERR_BAD_VERSION; 
  }

  if (cksum != start.cksum) 
  {
    return NP_ERR_CHECKSUM_FAILED; 
  }

  return 0; 
}

/** The on-disk format is packet_start followed by the newest version of the status struct. 
 *
 * Note that the implementation of status and header are basically the same right now... but that might
 * change if one of the versions changes. 
 */
static int nuphase_status_generic_write(struct generic_file gf, const nuphase_status_t *st) 
{
  struct packet_start start; 
  int written; 
  start.magic = NUPHASE_STATUS_MAGIC; 
  start.ver = NUPHASE_STATUS_VERSION; 
  start.cksum = stupid_fletcher16(sizeof(nuphase_status_t), st); 

  written = generic_write(gf, sizeof(start), &start); 
  if (written != sizeof(start)) 
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  written = generic_write(gf, sizeof(nuphase_status_t), st); 
  
  if (written != sizeof(nuphase_status_t))
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  return 0; 
}

static int nuphase_status_generic_read(struct generic_file gf, nuphase_status_t *st) 
{
  struct packet_start start; 
  int got; 
  int wanted; 
  uint16_t cksum; 

  got = packet_start_read(gf, &start, NUPHASE_STATUS_MAGIC, NUPHASE_STATUS_VERSION); 
  if (got) return got; 

  switch(start.ver) 
  {
    //add cases here if necessary 
   case NUPHASE_STATUS_VERSION: //this is the most recent status!
      wanted = sizeof(nuphase_status_t); 
      got = generic_read(gf, wanted, st); 
      cksum = stupid_fletcher16(wanted, st); 
      break; 
    default: 
    return NP_ERR_BAD_VERSION; 
  }

  if (wanted!=got)
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  if (cksum != start.cksum) 
  {
    return NP_ERR_CHECKSUM_FAILED; 
  }

  return 0; 
}

static int nuphase_hk_generic_write(struct generic_file gf, const nuphase_hk_t *hk)
{
  struct packet_start start; 
  int written; 
  start.magic = NUPHASE_HK_MAGIC; 
  start.ver = NUPHASE_HK_VERSION; 
  start.cksum = stupid_fletcher16(sizeof(nuphase_hk_t), hk); 

  written = generic_write(gf, sizeof(start), &start); 
  if (written != sizeof(start)) 
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  written = generic_write(gf, sizeof(nuphase_hk_t), hk); 
  
  if (written != sizeof(nuphase_hk_t))
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  return 0; 
}

static int nuphase_hk_generic_read(struct generic_file gf, nuphase_hk_t *hk) 
{
  struct packet_start start; 
  int got; 
  int wanted; 
  uint16_t cksum; 

  got = packet_start_read(gf, &start, NUPHASE_HK_MAGIC, NUPHASE_HK_VERSION); 
  if (got) return got; 

  switch(start.ver) 
  {
    //add cases here if necessary 
    case NUPHASE_HK_VERSION: //this is the most recent hk!
      wanted = sizeof(nuphase_hk_t); 
      got = generic_read(gf, wanted, hk); 
      cksum = stupid_fletcher16(wanted, hk); 
      break; 
    default: 
    return NP_ERR_BAD_VERSION; 
  }

  if (wanted!=got)
  {
    return NP_ERR_NOT_ENOUGH_BYTES; 
  }

  if (cksum != start.cksum) 
  {
    return NP_ERR_CHECKSUM_FAILED; 
  }

  return 0; 
}



/* 
 * these should all probably be generated by a macro instead of my copy-paste job...
 **/

int nuphase_event_write(FILE * f, const nuphase_event_t * ev) 
{
  struct generic_file gf=  { .type = STDIO, .handle.f = f }; 
  return nuphase_event_generic_write(gf, ev); 
}

int nuphase_event_gzwrite(gzFile f, const nuphase_event_t * ev) 
{
  struct generic_file gf=  { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_event_generic_write(gf, ev); 
}

int nuphase_event_read(FILE * f, nuphase_event_t * ev) 
{
  struct generic_file gf=  { .type = STDIO, .handle.f = f }; 
  return nuphase_event_generic_read(gf, ev); 
}

int nuphase_event_gzread(gzFile f, nuphase_event_t * ev) 
{
  struct generic_file gf=  { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_event_generic_read(gf, ev); 
}

int nuphase_status_write(FILE * f, const nuphase_status_t * ev) 
{
  struct generic_file gf=  { .type = STDIO, .handle.f = f }; 
  return nuphase_status_generic_write(gf, ev); 
}

int nuphase_status_gzwrite(gzFile f, const nuphase_status_t * ev) 
{
  struct generic_file gf=  { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_status_generic_write(gf, ev); 
}

int nuphase_status_read(FILE * f, nuphase_status_t * ev) 
{
  struct generic_file gf=  { .type = STDIO, .handle.f = f }; 
  return nuphase_status_generic_read(gf, ev); 
}

int nuphase_status_gzread(gzFile f, nuphase_status_t * ev) 
{
  struct generic_file gf=  { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_status_generic_read(gf, ev); 
}

int nuphase_header_write(FILE * f, const nuphase_header_t * h) 
{
  struct generic_file gf = { .type = STDIO, .handle.f = f }; 
  return nuphase_header_generic_write(gf, h); 
}

int nuphase_header_gzwrite(gzFile f, const nuphase_header_t * h) 
{
  struct generic_file gf = { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_header_generic_write(gf, h); 
}

int nuphase_header_read(FILE * f, nuphase_header_t * h) 
{
  struct generic_file gf = { .type = STDIO, .handle.f = f }; 
  return nuphase_header_generic_read(gf, h); 
}

int nuphase_header_gzread(gzFile f, nuphase_header_t * h) 
{
  struct generic_file gf = { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_header_generic_read(gf, h); 
}

int nuphase_hk_write(FILE * f, const nuphase_hk_t * h) 
{
  struct generic_file gf = { .type = STDIO, .handle.f = f }; 
  return nuphase_hk_generic_write(gf, h); 
}

int nuphase_hk_gzwrite(gzFile f, const nuphase_hk_t * h) 
{
  struct generic_file gf = { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_hk_generic_write(gf, h); 
}

int nuphase_hk_read(FILE * f, nuphase_hk_t * h) 
{
  struct generic_file gf = { .type = STDIO, .handle.f = f }; 
  return nuphase_hk_generic_read(gf, h); 
}

int nuphase_hk_gzread(gzFile f, nuphase_hk_t * h) 
{
  struct generic_file gf = { .type = ZLIB, .handle.gzf = f }; 
  return nuphase_hk_generic_read(gf, h); 
}




/* pretty prints */ 

int nuphase_status_print(FILE *f, const nuphase_status_t *st)
{
  int i ; 
  struct tm * tim; 
  char timstr[128]; 
  time_t t = st->readout_time;
  tim = gmtime((time_t*) &t); 
  strftime(timstr,sizeof(timstr), "%Y-%m-%d %H:%M:%S", tim);  
  fprintf(f,"NuPhase Board 0x%x Status (read at %s.%09d UTC)\n", st->board_id, timstr, st->readout_time_ns); 
  fprintf(f,"latched pps: %"PRIu64"  \n", st->latched_pps_time); 

  fprintf(f,"\t which \t 0.1 Hz, gated 0.1Hz, 1 Hz, threshold\n"); 
  fprintf(f,"\tGLOBAL: \t%u \t%u \t%u\n", st->global_scalers[SCALER_SLOW], st->global_scalers[SCALER_SLOW_GATED], st->global_scalers[SCALER_FAST]); 
  for (i = 0; i < NP_NUM_BEAMS; i++)
  {
    fprintf(f,"\tBEAM %d: \t%u \t%u \t%u \t%u \n",i, st->beam_scalers[SCALER_SLOW][i], st->beam_scalers[SCALER_SLOW_GATED][i], st->beam_scalers[SCALER_FAST][i], st->trigger_thresholds[i] ); 
  }
  return 0; 
}

static const char * trig_type_names[4]  = { "NONE", "SW", "RF" ,"EXT" } ; 


int nuphase_header_print(FILE *f, const nuphase_header_t *hd)
{
  int i; 
  struct tm* tim; 
  time_t t; 
  char timstr[128]; 

  fprintf(f, "EVENT_NUMBER %"PRIu64"\n", hd->event_number ); 
  fprintf(f, "\t%s TRIGGER\n", trig_type_names[hd->trig_type]); 
  fprintf(f,  "\ttrig num: %"PRIu64" boards:", hd->trig_number); 
  for (i = 0; i < NP_MAX_BOARDS; i++) 
  {
    fprintf(f, " %d", hd->board_id[i]); 
  }
  fprintf(f , " sync_problem: %x\n", hd->sync_problem); 
  fprintf(f, "\tbuf len: %u ; pretrig: %u\n", hd->buffer_length, hd->pretrigger_samples); 
  fprintf(f,"\tbuf num: %u, buf_mask: %x\n", hd->buffer_number, hd->buffer_mask); 
  for (i = 0; i < NP_MAX_BOARDS; i++) 
  {
    if (!hd->board_id[i]) continue; 
    t = hd->readout_time[i];
    tim = gmtime((time_t*) &t); 
    strftime(timstr,sizeof(timstr), "%Y-%m-%d %H:%M:%S", tim);  
    fprintf(f, "\tbd %d rdout time: %s.%09d UTC\n",hd->board_id[i],timstr, hd->readout_time_ns[i]); 
  }

  for (i = 0; i < NP_MAX_BOARDS; i++) 
  {
    if (hd->board_id[i])
      fprintf(f, "\tbd %d trig time (raw): %"PRIu64"\n", hd->board_id[i], hd->trig_time[i]); 
  }

  t = hd->approx_trigger_time; 
  tim = gmtime((time_t*) &t); 
  strftime(timstr,sizeof(timstr), "%Y-%m-%d %H:%M:%S", tim);  
  fprintf(f, "\ttrig time (est.): %s.%09d UTC\n",timstr, hd->approx_trigger_time_nsecs); 
  fprintf(f, "\ttrig beams: %x\n", hd->triggered_beams); 
  fprintf(f, "\tenabld beams: %x\n", hd->beam_mask); 
  fprintf(f, "\ttriggered beam power: %u\n", hd->beam_power) ; 
  fprintf(f,"\tprev sec deadtime: ");
  for (i = 0; i < NP_MAX_BOARDS; i++)
  {
    if (hd->board_id[i]) 
      fprintf(f," %u", hd->deadtime[i]); 
  }

  fprintf(f,"\n\ttrig_channel_mask: %x\n", hd->channel_mask); 
  fprintf(f,"\tchannel_read_mask: \n"); 
  for (i = 0; i < NP_MAX_BOARDS; i++)
  {
    if (hd->board_id[i]) 
      fprintf(f," %x", hd->channel_read_mask[i]); 
  }


  fprintf(f,"\n\tcalpulser: %s\n", hd->calpulser ? "yes" : "no"); 
  fprintf(f,"\tgate?: %s\n", hd->gate_flag & 1 ? "yes" : "no"); 
  

  return 0; 
}


int nuphase_event_print(FILE *f, const nuphase_event_t *ev, char sep)
{
  int ichan, isamp, ibd ; 
  for (ibd = 0; ibd < NP_MAX_BOARDS; ibd++)
  {
    if (!ev->board_id[ibd]) continue;
    fprintf(f, "EVENT NUMBER:%c %"PRIu64" %c BOARD: %c %d %c LENGTH: %c %d \n", sep,ev->event_number,sep,sep,ev->board_id[ibd], sep,sep,ev->buffer_length ); 
    for (ichan = 0; ichan < NP_NUM_CHAN; ichan++)
    {
      for (isamp = 0; isamp < ev->buffer_length; isamp++) 
      {
        fprintf(f, "%d%c", ev->data[ibd][ichan][isamp], isamp < ev->buffer_length - 1 ? sep : '\n'); 
      }
    }
  }

  return 0; 
}

int nuphase_hk_print(FILE * f, const nuphase_hk_t *hk) 
{
  struct tm*  tim; 
  char timstr[128]; 
  time_t t = hk->unixTime;
  tim = gmtime(&t); 
  strftime(timstr,sizeof(timstr), "%Y-%m-%d %H:%M:%S", tim);  
  fprintf(f,"HK (at %s.%03d UTC)\n", timstr, hk->unixTimeMillisecs); 
  fprintf(f,"  Temperatures: \n"); 

  if (hk->temp_board > -128)
  {
    fprintf(f,"      BOARD:  %d C\n", hk->temp_board); 
  }
  else
  {
    fprintf(f,"      BOARD: sensor off\n"); 
  }

  if (hk->temp_adc_0 > -128)
  {
    fprintf(f,"      ADC0:  %d C\n", hk->temp_adc_0); 
  }
  else
  {
    fprintf(f,"      ADC0: sensor off\n"); 
  }

  if (hk->temp_adc_1 > -128)
  {
    fprintf(f,"      ADC1:  %d C\n", hk->temp_adc_1); 
  }
  else
  {
    fprintf(f,"      ADC1: sensor off\n"); 
  }


  fprintf(f,"  Currents:\n"); 
  fprintf(f,"      ADC: %hu mA", hk->adc_current); 
  fprintf(f,"      FRNTND: %hu mA", hk->frontend_current); 
  fprintf(f,"      AUX: %hu mA", hk->aux_current); 
  fprintf(f,"      ANT: %hu mA", hk->ant_current); 

  fprintf(f,"      MASTER_FPGA:  %s \n", (hk->gpio_state & NP_FPGA_POWER_MASTER)   ? "ON ":"OFF"); 
  fprintf(f,"      SPI        :  %s \n", (hk->gpio_state & NP_SPI_ENABLE)   ? "ON ":"OFF"); 
  fprintf(f,"  SBC: \n"); 
  fprintf(f,"     DISK SPACE: %0.3g MB \n", hk->disk_space_kB /1024.);  
  fprintf(f,"     FREE MEM  : %0.3g MB \n", hk->free_mem_kB   /1024.);  

  return 0; 
}


