/* Copyright (c) 2009-2011 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

/*
    An Open max test application for QCELP13 Encoding....
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "QOMX_AudioExtensions.h"
#include "QOMX_AudioIndexExtensions.h"
#ifdef AUDIOV2
#include "control.h"
#endif
#include <linux/ioctl.h>
#include "OMX_Core.h"
#include "OMX_Component.h"

typedef unsigned char uint8;
typedef unsigned char byte;
typedef unsigned int  uint32;
typedef unsigned int  uint16;

void Release_Encoder();
#ifdef AUDIOV2
unsigned short session_id;
int device_id;
int control = 0;
const char *device="handset_tx";
int devmgr_fd;
#define DIR_TX 2
#endif
FILE *F1 = NULL;

uint32_t samplerate = 8000;
uint32_t channels = 1;
uint32_t min_bitrate = 0;
uint32_t max_bitrate = 0;
uint32_t cdmarate = 0;
uint32_t rectime = 0;
uint32_t recpath = -1;
uint32_t tunnel = 0;
#define DEBUG_PRINT(args...) printf("%s:%d ", __FUNCTION__, __LINE__); \
                             printf(args)

#define DEBUG_PRINT_ERROR(args...) printf("%s:%d ", __FUNCTION__, __LINE__); \
                       printf(args)

/************************************************************************/
/*                #DEFINES                            */
/************************************************************************/
#define false 0
#define true 1
#define CONFIG_VERSION_SIZE(param) \
    param.nVersion.nVersion = CURRENT_OMX_SPEC_VERSION;\
    param.nSize = sizeof(param);
#define FAILED(result) (result != OMX_ErrorNone)
#define SUCCEEDED(result) (result == OMX_ErrorNone)
#define QCP_HEADER_SIZE sizeof(struct qcp_header)
#define MIN_BITRATE 1
#define MAX_BITRATE 4



/************************************************************************/
/*                GLOBAL DECLARATIONS                     */
/************************************************************************/

pthread_mutex_t lock;
pthread_cond_t cond;
pthread_mutex_t elock;
pthread_cond_t econd;

pthread_cond_t fcond;
pthread_mutex_t etb_lock;
pthread_mutex_t etb_lock1;
pthread_cond_t etb_cond;
const char *in_filename;
FILE * inputBufferFile;
FILE * outputBufferFile;
OMX_PARAM_PORTDEFINITIONTYPE outputportFmt;
OMX_PARAM_PORTDEFINITIONTYPE inputportFmt;
OMX_AUDIO_PARAM_QCELP13TYPE qcelp13param;
OMX_AUDIO_PARAM_PCMMODETYPE    pcmparam;
OMX_PORT_PARAM_TYPE portParam;
OMX_PORT_PARAM_TYPE portFmt;
OMX_ERRORTYPE error;

static unsigned totaldatalen = 0;
static unsigned framecnt = 0;
#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164
#define FORMAT_PCM 1

struct wav_header {
  uint32_t riff_id;
  uint32_t riff_sz;
  uint32_t riff_fmt;
  uint32_t fmt_id;
  uint32_t fmt_sz;
  uint16_t audio_format;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;       /* sample_rate * num_channels * bps / 8 */
  uint16_t block_align;     /* num_channels * bps / 8 */
  uint16_t bits_per_sample;
  uint32_t data_id;
  uint32_t data_sz;
};
struct qcp_header {
        /* RIFF Section */
        char riff[4];
        unsigned int s_riff;
        char qlcm[4];

        /* Format chunk */
        char fmt[4];
        unsigned int s_fmt;
        char mjr;
        char mnr;
        unsigned int data1;         /* UNIQUE ID of the codec */
        unsigned short data2;
        unsigned short data3;
        char data4[8];
        unsigned short ver;         /* Codec Info */
        char name[80];
        unsigned short abps;    /* average bits per sec of the codec */
        unsigned short bytes_per_pkt;
        unsigned short samp_per_block;
        unsigned short samp_per_sec;
        unsigned short bits_per_samp;
        unsigned char vr_num_of_rates;         /* Rate Header fmt info */
        unsigned char rvd1[3];
        unsigned short vr_bytes_per_pkt[8];
        unsigned int rvd2[5];

        /* Vrat chunk */
        unsigned char vrat[4];
        unsigned int s_vrat;
        unsigned int v_rate;
        unsigned int size_in_pkts;

        /* Data chunk */
        unsigned char data[4];
        unsigned int s_data;
} __attribute__ ((packed));

 /* Common part */
 static struct qcp_header append_header = {
         {'R', 'I', 'F', 'F'}, 0, {'Q', 'L', 'C', 'M'},
         {'f', 'm', 't', ' '}, 150, 1, 0, 0, 0, 0,{0}, 0, {0},0,0,160,8000,16,0,{0},{0},{0},
         {'v','r','a','t'},0, 0, 0,{'d','a','t','a'},0
 };
/************************************************************************/
/*                GLOBAL INIT                    */
/************************************************************************/
int ebd_cnt;
int bOutputEosReached = 0;
int bInputEosReached_tunnel = 0;
int bInputEosReached = 0;
static int etb_done = 0;
int bFlushing = false;
int bPause    = false;

int input_buf_cnt = 0;
int output_buf_cnt = 0;
int used_ip_buf_cnt = 0; //TBD:
volatile int event_is_done = 0;
volatile int fbd_event_is_done = 0;
volatile int etb_event_is_done = 0;
const char *out_filename;
int timeStampLfile = 0;
int timestampInterval = 100;
unsigned to_idle_transition = 0;
unsigned long total_pcm_bytes;
QOMX_AUDIO_STREAM_INFO_DATA streaminfoparam;
QOMX_AUDIO_CONFIG_VOICERECORDTYPE recinfoparam;
//* OMX Spec Version supported by the wrappers. Version = 1.1 */
const OMX_U32 CURRENT_OMX_SPEC_VERSION = 0x00000101;
OMX_COMPONENTTYPE* qcelp13_enc_handle = 0;
OMX_BUFFERHEADERTYPE  **pInputBufHdrs = NULL;
OMX_BUFFERHEADERTYPE  **pOutputBufHdrs = NULL;

/************************************************************************/
/*                GLOBAL FUNC DECL                        */
/************************************************************************/
int Init_Encoder();
int Rec_Encoder();
OMX_STRING aud_comp;

/**************************************************************************/
/*                STATIC DECLARATIONS                       */
/**************************************************************************/

static int open_output_file ();
static int Read_Buffer(OMX_BUFFERHEADERTYPE  *pBufHdr );
static void write_devctlcmd(int fd, const void *buf, unsigned short param);
static OMX_ERRORTYPE Allocate_Buffer (OMX_COMPONENTTYPE *qcelp13_enc_handle,
                                      OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                      OMX_U32 nPortIndex,
                                      long bufCntMin, long bufSize);

static OMX_ERRORTYPE EventHandler (OMX_IN OMX_HANDLETYPE hComponent,
                                   OMX_IN OMX_PTR pAppData,
                                   OMX_IN OMX_EVENTTYPE eEvent,
                                   OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                                   OMX_IN OMX_PTR pEventData);

static OMX_ERRORTYPE FillBufferDone (OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_PTR pAppData,
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_PTR pAppData,
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE  parse_pcm_header( void);
void wait_for_event(void)
{
    pthread_mutex_lock(&lock);
    DEBUG_PRINT("%s: event_is_done=%d", __FUNCTION__, event_is_done);
    while (event_is_done == 0) {
        pthread_cond_wait(&cond, &lock);
    }
    event_is_done = 0;
    pthread_mutex_unlock(&lock);
}

void event_complete(void )
{
    pthread_mutex_lock(&lock);
    if (event_is_done == 0) {
        event_is_done = 1;
        pthread_cond_broadcast(&cond);
    }
    pthread_mutex_unlock(&lock);
}
void etb_wait_for_event(void)
{
    pthread_mutex_lock(&etb_lock1);
    DEBUG_PRINT("%s: etb_event_is_done=%d", __FUNCTION__, etb_event_is_done);
    while (etb_event_is_done == 0) {
        pthread_cond_wait(&etb_cond, &etb_lock1);
    }
    etb_event_is_done = 0;
    pthread_mutex_unlock(&etb_lock1);
}

void etb_event_complete(void )
{
    pthread_mutex_lock(&etb_lock1);
    if (etb_event_is_done == 0) {
        etb_event_is_done = 1;
        pthread_cond_broadcast(&etb_cond);
    }
    pthread_mutex_unlock(&etb_lock1);
}

static void create_qcp_header(int Datasize, int Frames)
{
        append_header.s_riff = Datasize + QCP_HEADER_SIZE - 8;
        /* exclude riff id and size field */
        append_header.data1 = 0x5E7F6D41;
        append_header.data2 = 0xB115;
        append_header.data3 = 0x11D0;
        append_header.data4[0] = 0xBA;
        append_header.data4[1] = 0x91;
        append_header.data4[2] = 0x00;
        append_header.data4[3] = 0x80;
        append_header.data4[4] = 0x5F;
        append_header.data4[5] = 0xB4;
        append_header.data4[6] = 0xB9;
        append_header.data4[7] = 0x7E;
        append_header.ver = 0x0002;
        memcpy(append_header.name, "Qcelp 13K", 9);
        append_header.abps = 13000;
        append_header.bytes_per_pkt = 35;
        append_header.vr_num_of_rates = 5;
        append_header.vr_bytes_per_pkt[0] = 0x0422;
        append_header.vr_bytes_per_pkt[1] = 0x0310;
        append_header.vr_bytes_per_pkt[2] = 0x0207;
        append_header.vr_bytes_per_pkt[3] = 0x0103;
        append_header.s_vrat = 0x00000008;
        append_header.v_rate = 0x00000001;
        append_header.size_in_pkts = Frames;
        append_header.s_data = Datasize;
        return;
 }

static int open_output_file()
{
    int error_code = 0;
    if (!tunnel)
    {
        DEBUG_PRINT("Inside %s filename=%s\n", __FUNCTION__, in_filename);
        inputBufferFile = fopen (in_filename, "rb");
        if (inputBufferFile == NULL) {
            DEBUG_PRINT("\ni/p file %s could NOT be opened\n",
                                         in_filename);
        error_code = -1;
        }
        if(parse_pcm_header() != 0x00)
        {
            DEBUG_PRINT("PCM parser failed \n");
            return -1;
        }
   }
    DEBUG_PRINT("Inside %s filename=%s\n", __FUNCTION__, out_filename);
    outputBufferFile = fopen (out_filename, "wb");
    fseek(outputBufferFile, QCP_HEADER_SIZE,SEEK_SET);
    if (outputBufferFile == NULL) {
        DEBUG_PRINT("\no/p file %s could NOT be opened\n",
                             out_filename);
        error_code = -1;
     }
     fseek(outputBufferFile, QCP_HEADER_SIZE, SEEK_SET);
     return error_code;
}

//In Encoder this Should Open a PCM or WAV file for input.
static int Read_Buffer (OMX_BUFFERHEADERTYPE  *pBufHdr )
{

    int bytes_read=0;


    pBufHdr->nFilledLen = 0;
    pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;

     bytes_read = fread(pBufHdr->pBuffer, 1, pBufHdr->nAllocLen , inputBufferFile);

      pBufHdr->nFilledLen = bytes_read;
      // Time stamp logic
    ((OMX_BUFFERHEADERTYPE *)pBufHdr)->nTimeStamp = \

    (unsigned long long ) (1000 * ((total_pcm_bytes * 1000)/(samplerate * channels *2)));

       DEBUG_PRINT ("\n-- i/p time stamp -- %llu\n",  (unsigned long long)(pBufHdr->nTimeStamp));
        if(bytes_read == 0)
        {
          pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
          DEBUG_PRINT ("\nBytes read zero\n");
        }
        else
        {
            pBufHdr->nFlags &= ~OMX_BUFFERFLAG_EOS;

            total_pcm_bytes += bytes_read;
        }

    return bytes_read;;
}

static OMX_ERRORTYPE parse_pcm_header( void)
{
    struct wav_header hdr;

    DEBUG_PRINT("\n***************************************************************\n");
    if(fread(&hdr, 1, sizeof(hdr),inputBufferFile)!=sizeof(hdr))
    {
        DEBUG_PRINT("Wav file cannot read header\n");
        return -1;
    }

    if ((hdr.riff_id != ID_RIFF) ||
        (hdr.riff_fmt != ID_WAVE)||
        (hdr.fmt_id != ID_FMT))
    {
        DEBUG_PRINT("Wav file is not a riff/wave file\n");
        return -1;
    }

    if (hdr.audio_format != FORMAT_PCM)
    {
        DEBUG_PRINT("Wav file is not adpcm format %d and fmt size is %d\n",
                      hdr.audio_format, hdr.fmt_sz);
        return -1;
    }

    DEBUG_PRINT("Samplerate is %d\n", hdr.sample_rate);
    DEBUG_PRINT("Channel Count is %d\n", hdr.num_channels);
    DEBUG_PRINT("\n***************************************************************\n");

    samplerate = hdr.sample_rate;
    channels = hdr.num_channels;
    total_pcm_bytes = 0;

    return OMX_ErrorNone;
}

static void write_devctlcmd(int fd, const void *buf, unsigned short param){
    int nbytes, nbytesWritten;
    char cmdstr[128];
    snprintf(cmdstr, 128, "%s%d\n", (char *)buf, param);
    nbytes = strlen(cmdstr);
    nbytesWritten = write(fd, cmdstr, nbytes);

    if(nbytes != nbytesWritten)
        printf("Failed to write string \"%s\" to omx_devmgr\n",cmdstr);
}
static OMX_ERRORTYPE Allocate_Buffer( OMX_COMPONENTTYPE *avc_enc_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize)
{
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;
    (void)avc_enc_handle;
    *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
           malloc(sizeof(OMX_BUFFERHEADERTYPE*)*bufCntMin);

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
        DEBUG_PRINT("\n OMX_AllocateBuffer No %ld \n", bufCnt);
        error = OMX_AllocateBuffer(qcelp13_enc_handle, &((*pBufHdrs)[bufCnt]),
                       nPortIndex, NULL, bufSize);
    }
    return error;
}
OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData,
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{
    int bufCnt=0;
    DEBUG_PRINT("Function %s \n", __FUNCTION__);
    (void)hComponent;
    (void)pAppData;
    (void)pEventData;
    switch(eEvent) {
        case OMX_EventCmdComplete:
            DEBUG_PRINT("\n OMX_EventCmdComplete event=%d data1=%lu data2=%lu %d\n",(OMX_EVENTTYPE)eEvent,
                nData1,nData2,to_idle_transition);
             event_complete();
             break;
	case OMX_EventBufferFlag:
             DEBUG_PRINT("\n OMX_EventBufferFlag event=%u data1=%lu data2=%lu\n",(OMX_EVENTTYPE)eEvent,
                nData1,nData2);
             break;
        case OMX_EventError:
             DEBUG_PRINT("\n OMX_EventError \n");
             if(OMX_ErrorInvalidState == (OMX_ERRORTYPE)nData1)
             {
               DEBUG_PRINT("\n OMX_ErrorInvalidState \n");
               for(bufCnt=0; bufCnt < output_buf_cnt; ++bufCnt)
               {
                   OMX_FreeBuffer(qcelp13_enc_handle, 1, pOutputBufHdrs[bufCnt]);
               }
               DEBUG_PRINT("*********************************************\n");
               DEBUG_PRINT("\n Component Deinitialized \n");
               DEBUG_PRINT("*********************************************\n");
               exit(0);
             }
             break;
        case OMX_EventPortSettingsChanged:
             DEBUG_PRINT("\n OMX_EventPortSettingsChanged \n");
             break;
        default:
            DEBUG_PRINT("\n Unknown Event \n");
            break;
    }
    return OMX_ErrorNone;
}


OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    int readBytes =0;

    /* To remove warning for unused variable to keep prototype same */
    (void)pAppData;

    ebd_cnt++;
    used_ip_buf_cnt--;
    pthread_mutex_lock(&etb_lock);
    if(!etb_done)
    {
        DEBUG_PRINT("\n*********************************************\n");
        DEBUG_PRINT("Wait till first set of buffers are given to component\n");
        DEBUG_PRINT("\n*********************************************\n");
        etb_done++;
        pthread_mutex_unlock(&etb_lock);
        etb_wait_for_event();
    }
    else
    {
        pthread_mutex_unlock(&etb_lock);
    }

    if(bInputEosReached)
    {
        DEBUG_PRINT("\n*********************************************\n");
        DEBUG_PRINT("   EBD::EOS on input port\n ");
        DEBUG_PRINT("*********************************************\n");
        return OMX_ErrorNone;
    }else if (bFlushing == true) {
      DEBUG_PRINT("omx_qcelp13_aenc_test: bFlushing is set to TRUE used_ip_buf_cnt=%d\n",used_ip_buf_cnt);
      if (used_ip_buf_cnt == 0) {
        bFlushing = false;
      } else {
        DEBUG_PRINT("omx_qcelp13_aenc_test: more buffer to come back used_ip_buf_cnt=%d\n",used_ip_buf_cnt);
        return OMX_ErrorNone;
      }
    }

    if((readBytes = Read_Buffer(pBuffer)) > 0) {
        pBuffer->nFilledLen = readBytes;
        used_ip_buf_cnt++;
        OMX_EmptyThisBuffer(hComponent,pBuffer);
    }
    else{
        pBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
        used_ip_buf_cnt++;
        bInputEosReached = true;
        pBuffer->nFilledLen = 0;
        OMX_EmptyThisBuffer(hComponent,pBuffer);
        DEBUG_PRINT("EBD..Either EOS or Some Error while reading file\n");
    }
    return OMX_ErrorNone;
}

void signal_handler(int sig_id) {

  /* Flush */
  if (sig_id == SIGUSR1) {
    DEBUG_PRINT("%s Initiate flushing\n", __FUNCTION__);
    bFlushing = true;
    OMX_SendCommand(qcelp13_enc_handle, OMX_CommandFlush, OMX_ALL, NULL);
  } else if (sig_id == SIGUSR2) {
    if (bPause == true) {
      DEBUG_PRINT("%s resume record\n", __FUNCTION__);
      bPause = false;
      OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    } else {
      DEBUG_PRINT("%s pause record\n", __FUNCTION__);
      bPause = true;
      OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StatePause, NULL);
    }
  }
}

OMX_ERRORTYPE FillBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    int bytes_writen = 0;
    static unsigned int  count = 0;
    unsigned char readBuf;
    static int releaseCount = 0;

    (void)pAppData;


    if(((pBuffer->nFlags & OMX_BUFFERFLAG_EOS) == OMX_BUFFERFLAG_EOS)) {
           DEBUG_PRINT("FBD::EOS on output port\n ");
           bOutputEosReached = true;
           event_complete();
           return OMX_ErrorNone;
    }

    if(bInputEosReached_tunnel || bOutputEosReached) {

        DEBUG_PRINT("EOS REACHED NO MORE PROCESSING OF BUFFERS\n");
        return OMX_ErrorNone;
    }

    DEBUG_PRINT(" FillBufferDone #%d size %lu\n", ++count,pBuffer->nFilledLen);
    DEBUG_PRINT ("\n-- o/p time stamp -- %llu\n",  (unsigned long long)(pBuffer->nTimeStamp));
    bytes_writen = fwrite(pBuffer->pBuffer,1,pBuffer->nFilledLen,outputBufferFile);
    if(bytes_writen < (signed)pBuffer->nFilledLen)
    {
        DEBUG_PRINT("error: invalid QCELP13 encoded data \n");
        return OMX_ErrorNone;
    }

    DEBUG_PRINT(" FillBufferDone size writen to file  %d\n",bytes_writen);
    totaldatalen += bytes_writen ;
    framecnt++;

    // Forceful exit
    if(!releaseCount)
    {
        if(read(0, &readBuf, 1) == 1)
        {
            if ((readBuf == 13) || (readBuf == 10))
            {
                printf("\n GOT THE ENTER KEY\n");
                releaseCount++;
            }
        }
    }
    if(releaseCount == 1 || rectime == 0)
    {
        // Dont issue any more FTB
        // Trigger Exe-->Idle Transition

        DEBUG_PRINT("Recieved Signal to stop recording...\n");
        sleep(1);
        releaseCount++;
        bOutputEosReached = 1;
        to_idle_transition = 1;
        event_complete();
        // wait till Idle transition is complete
        // Trigger ReleaseEncoder procedure
    }
    DEBUG_PRINT(" FBD calling FTB");
    OMX_FillThisBuffer(hComponent,pBuffer);

    return OMX_ErrorNone;
}


int main(int argc, char **argv)
{
    int bufCnt=0;
    OMX_ERRORTYPE result;
   struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler;
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    (void) signal(SIGINT, Release_Encoder);

    pthread_cond_init(&cond, 0);
    pthread_mutex_init(&lock, 0);
    pthread_cond_init(&etb_cond, 0);
    pthread_mutex_init(&etb_lock, 0);
    pthread_mutex_init(&etb_lock1, 0);

    if (argc >= 7) {
        out_filename = argv[1];
        min_bitrate  = atoi(argv[2]);
        max_bitrate  = atoi(argv[3]);
        cdmarate     = atoi(argv[4]);
        recpath      = atoi(argv[5]);
        rectime      = atoi(argv[6]);
        if ( argc >=9) {
            in_filename = argv[7];
            tunnel =  atoi(argv[8]);
        }

#ifndef AUDIOV2
        if(recpath == 3 ) {
            DEBUG_PRINT("Please use RECORDPATH 0(TX),1(RX),2(BOTH) for Mic recording\n");
            return 0;
        }
#endif
    } else {
          DEBUG_PRINT(" invalid format: \n");
          DEBUG_PRINT("ex: ./mm-aenc-omxqcelp13-test OUTPUTFILE MINRATE MAXRATE CDMARATE RECORDPATH RECORDTIME INPUTFILE Tunnel\n");
          DEBUG_PRINT("Bitrate and Cdmarate 1-4\n");
          DEBUG_PRINT("RECORDPATH 0(TX),1(RX),2(BOTH),3(MIC)\n");
          DEBUG_PRINT("RECORDTIME in seconds for AST Automation\n");
          return 0;
    }

    if(tunnel == 0)
        aud_comp = "OMX.qcom.audio.encoder.qcelp13";
    else
        aud_comp = "OMX.qcom.audio.encoder.tunneled.qcelp13";
    if(Init_Encoder(aud_comp)!= 0x00) {
        DEBUG_PRINT("Encoder Init failed\n");
        return -1;
    }
    fcntl(0, F_SETFL, O_NONBLOCK);

    if(Rec_Encoder() != 0x00) {
        DEBUG_PRINT("Rec_Encoder failed\n");
        return -1;
    }
    // Wait till EOS is reached...
    if(rectime  && tunnel)
    {
        DEBUG_PRINT("Before rectime sleep\n");
        sleep(rectime);
        rectime = 0;
        bInputEosReached_tunnel = 1;
        DEBUG_PRINT("After rectime sleep\n");
    }
    else
    {
         wait_for_event();
    }
    if((bInputEosReached_tunnel) || ((bOutputEosReached) && !tunnel))
    {
        OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StateIdle,0);
        wait_for_event();
        DEBUG_PRINT("\nMoving the encoder to loaded state \n");
        OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StateLoaded,0);
        sleep(1);
        if (!tunnel)
        {
             DEBUG_PRINT("\nDeallocating i/p buffers \n");
             for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt) {
                  OMX_FreeBuffer(qcelp13_enc_handle, 0, pInputBufHdrs[bufCnt]);
             }
        }
        DEBUG_PRINT ("\nDeallocating o/p buffers \n");
        for(bufCnt=0; bufCnt < output_buf_cnt; ++bufCnt) {
            OMX_FreeBuffer(qcelp13_enc_handle, 1, pOutputBufHdrs[bufCnt]);
        }
        wait_for_event();
        create_qcp_header(totaldatalen, framecnt);
    	fseek(outputBufferFile, 0,SEEK_SET);
        fwrite(&append_header,1,QCP_HEADER_SIZE,outputBufferFile);

        result = OMX_FreeHandle(qcelp13_enc_handle);
        if (result != OMX_ErrorNone) {
            DEBUG_PRINT ("\nOMX_FreeHandle error. Error code: %d\n", result);
        }

        /* Deinit OpenMAX */
        if(tunnel)
        {
#ifdef AUDIOV2
		if(devmgr_fd >= 0)
		{
			write_devctlcmd(devmgr_fd, "-cmd=unregister_session_tx -sid=", session_id);
			close(devmgr_fd);
		}
		else
		{
           #ifdef AUDIOV2
			if (msm_route_stream(DIR_TX,session_id,device_id, 0))
			{
			DEBUG_PRINT("\ncould not set stream routing\n");
				return -1;
			}
			if (msm_en_device(device_id, 0))
			{
				DEBUG_PRINT("\ncould not enable device\n");
				return -1;
			}
			msm_mixer_close();
            #endif
		}
 #endif
        }

       /* Deinit OpenMAX */
       OMX_Deinit();
       ebd_cnt=0;
       bOutputEosReached = false;
       bInputEosReached_tunnel = false;
       bInputEosReached = 0;
       qcelp13_enc_handle = NULL;
       pthread_cond_destroy(&cond);
       pthread_mutex_destroy(&lock);
       fclose(outputBufferFile);
       DEBUG_PRINT("*****************************************\n");
       DEBUG_PRINT("******...QCELP13 ENC TEST COMPLETED...***************\n");
       DEBUG_PRINT("*****************************************\n");
      }
        return 0;
}

void Release_Encoder()
{
    static int cnt=0;
    OMX_ERRORTYPE result;
    DEBUG_PRINT("END OF QCELP13 ENCODING: EXITING PLEASE WAIT\n");
    bInputEosReached = 1;
    bInputEosReached_tunnel = 1;
    event_complete();
    cnt++;
    if(cnt > 1) {

        /* FORCE RESET  */
        qcelp13_enc_handle = NULL;
        bInputEosReached = false;
            result = OMX_FreeHandle(qcelp13_enc_handle);
        if (result != OMX_ErrorNone) {
        DEBUG_PRINT ("\nOMX_FreeHandle error. Error code: %d\n", result);
        }
            /* Deinit OpenMAX */
            OMX_Deinit();
            pthread_cond_destroy(&cond);
            pthread_mutex_destroy(&lock);
        DEBUG_PRINT("*****************************************\n");
        DEBUG_PRINT("******...QCELP13 ENC TEST COMPLETED...***************\n");
        DEBUG_PRINT("*****************************************\n");
        exit(0);
    }
}

int Init_Encoder(OMX_STRING audio_component)
{
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE omxresult;
    OMX_U32 total = 0;
    typedef OMX_U8* OMX_U8_PTR;
    char *role ="audio_encoder.qcelp13";
    static OMX_CALLBACKTYPE call_back = {
    &EventHandler,&EmptyBufferDone,&FillBufferDone
    };

    /* Init. the OpenMAX Core */
    DEBUG_PRINT("\nInitializing OpenMAX Core....\n");
    omxresult = OMX_Init();
    if(OMX_ErrorNone != omxresult) {
        DEBUG_PRINT("\n Failed to Init OpenMAX core");
        return -1;
    } else {
        DEBUG_PRINT("\nOpenMAX Core Init Done\n");
    }
    /* Query for audio encoders*/
    DEBUG_PRINT("QCELP13_test: Before entering OMX_GetComponentOfRole");
    OMX_GetComponentsOfRole(role, &total, 0);
    DEBUG_PRINT ("\nTotal components of role= :%s :%d", role, (int)total);
    omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&qcelp13_enc_handle),
            audio_component, NULL, &call_back);
         printf("test1\n");
    if (FAILED(omxresult)) {
        DEBUG_PRINT ("\nFailed to Load the component\n");
        return -1;
    } else {
        DEBUG_PRINT ("\nComponent is in LOADED state\n");
    }
    /* Get the port information */
    CONFIG_VERSION_SIZE(portParam);
    omxresult = OMX_GetParameter(qcelp13_enc_handle, OMX_IndexParamAudioInit,
                (OMX_PTR)&portParam);

    if(FAILED(omxresult)) {
        DEBUG_PRINT("\nFailed to get Port Param\n");
        return -1;
    } else {
        DEBUG_PRINT ("\nportParam.nPorts:%lu\n", portParam.nPorts);
            DEBUG_PRINT ("\nportParam.nStartPortNumber:%lu\n",
                         portParam.nStartPortNumber);
    }
    return 0;
}

int Rec_Encoder()
{
    int i, Size;
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE ret;
    OMX_INDEXTYPE index;
    OMX_INDEXTYPE recPathIndex;
    DEBUG_PRINT("sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));
    if(open_output_file()) {
        DEBUG_PRINT("\n Returning -1");
        return -1;
    }
    /* Query the encoder input min buf requirements */
    CONFIG_VERSION_SIZE(inputportFmt);

    /* Port for which the Client needs to obtain info */
    inputportFmt.nPortIndex = portParam.nStartPortNumber;

    OMX_GetParameter(qcelp13_enc_handle,OMX_IndexParamPortDefinition,&inputportFmt);
    DEBUG_PRINT ("\nEnc Input Buffer Count %lu\n", inputportFmt.nBufferCountMin);
    DEBUG_PRINT ("\nEnc: Input Buffer Size %lu\n", inputportFmt.nBufferSize);

    if(OMX_DirInput != inputportFmt.eDir) {
        DEBUG_PRINT ("\nEnc: Expect Input Port\n");
        return -1;
    }

    pcmparam.nPortIndex   = 0;
    pcmparam.nChannels    =  channels;
    pcmparam.nSamplingRate = samplerate;
    OMX_SetParameter(qcelp13_enc_handle,OMX_IndexParamAudioPcm,&pcmparam);

    /* Query the encoder outport's min buf requirements */
    CONFIG_VERSION_SIZE(outputportFmt);
    /* Port for which the Client needs to obtain info */
    outputportFmt.nPortIndex = portParam.nStartPortNumber + 1;

    OMX_GetParameter(qcelp13_enc_handle,OMX_IndexParamPortDefinition,&outputportFmt);
    DEBUG_PRINT ("\nEnc: Output Buffer Count %lu\n", outputportFmt.nBufferCountMin);
    DEBUG_PRINT ("\nEnc: Output Buffer Size %lu\n", outputportFmt.nBufferSize);

    if(OMX_DirOutput != outputportFmt.eDir) {
        DEBUG_PRINT ("\nEnc: Expect Output Port\n");
        return -1;
    }
    CONFIG_VERSION_SIZE(qcelp13param);
    qcelp13param.nPortIndex   =  1;
    qcelp13param.nChannels    =  channels; //2 ; /* 1-> mono 2-> stereo*/
    qcelp13param.nMinBitRate = min_bitrate;
    qcelp13param.nMaxBitRate = max_bitrate;
    qcelp13param.eCDMARate = cdmarate;
    DEBUG_PRINT("min rate = %d max rate = %d cdma rate = %d\n", min_bitrate, max_bitrate, cdmarate);



    OMX_SetParameter(qcelp13_enc_handle,OMX_IndexParamAudioQcelp13,&qcelp13param);
    OMX_GetExtensionIndex(qcelp13_enc_handle,
                          OMX_QCOM_INDEX_PARAM_SESSIONID,&index);
    OMX_GetParameter(qcelp13_enc_handle,index,&streaminfoparam);

    OMX_GetExtensionIndex(qcelp13_enc_handle,
                          OMX_QCOM_INDEX_PARAM_VOICERECORDTYPE,&recPathIndex);
    recinfoparam.eVoiceRecordMode = recpath;
    OMX_SetParameter(qcelp13_enc_handle,recPathIndex,&recinfoparam);

#ifdef AUDIOV2
    session_id = streaminfoparam.sessionId;
    devmgr_fd = open("/data/omx_devmgr", O_WRONLY);
    if(devmgr_fd >= 0)
    {
        control = 0;
        write_devctlcmd(devmgr_fd, "-cmd=register_session_tx -sid=", session_id);
    }
    else
    {
        if(tunnel) {
            control = msm_mixer_open("/dev/snd/controlC0", 0);
            if(control < 0)
                printf("ERROR opening the device\n");
            device_id = msm_get_device(device);
            DEBUG_PRINT ("\ndevice_id = %d\n",device_id);
            DEBUG_PRINT("\nsession_id = %d\n",session_id);
            if (msm_en_device(device_id, 1))
            {
                perror("could not enable device\n");
                return -1;
            }
            if (msm_route_stream(DIR_TX,session_id,device_id, 1))
            {
                perror("could not set stream routing\n");
                return -1;
            }
        }
    }
#endif
    DEBUG_PRINT ("\nOMX_SendCommand Encoder -> IDLE\n");
    OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StateIdle,0);
    /* wait_for_event(); should not wait here event complete status will
    not come until enough buffer are allocated */
    if (!tunnel)
    {
        input_buf_cnt = inputportFmt.nBufferCountActual;
        DEBUG_PRINT("Transition to Idle State succesful...\n");
        /* Allocate buffer on decoder's i/p port */
        error = Allocate_Buffer(qcelp13_enc_handle, &pInputBufHdrs, inputportFmt.nPortIndex,
                            input_buf_cnt, inputportFmt.nBufferSize);
        if (error != OMX_ErrorNone) {
            DEBUG_PRINT ("\nOMX_AllocateBuffer Input buffer error\n");
        return -1;
        }
    }
    output_buf_cnt = outputportFmt.nBufferCountMin ;
    /* Allocate buffer on encoder's O/P port */
    error = Allocate_Buffer(qcelp13_enc_handle, &pOutputBufHdrs, outputportFmt.nPortIndex,
                output_buf_cnt, outputportFmt.nBufferSize);
    if (error != OMX_ErrorNone) {
        DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer error\n");
        return -1;
    }
    else {
        DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer success\n");
    }
    wait_for_event();

    if (tunnel == 1)
    {
        DEBUG_PRINT ("\nOMX_SendCommand OMX_CommandPortDisable input port\n");
        OMX_SendCommand(qcelp13_enc_handle, OMX_CommandPortDisable,0,0); // disable input port
        wait_for_event();
    }
    DEBUG_PRINT ("\nOMX_SendCommand encoder -> Executing\n");
    OMX_SendCommand(qcelp13_enc_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
    wait_for_event();
    DEBUG_PRINT(" Start sending OMX_FILLthisbuffer\n");
    for(i=0; i < output_buf_cnt; i++) {
        DEBUG_PRINT ("\nOMX_FillThisBuffer on output buf no.%d\n",i);
        pOutputBufHdrs[i]->nOutputPortIndex = 1;
        pOutputBufHdrs[i]->nFlags &= ~OMX_BUFFERFLAG_EOS;
        ret = OMX_FillThisBuffer(qcelp13_enc_handle, pOutputBufHdrs[i]);
        if (OMX_ErrorNone != ret) {
            DEBUG_PRINT("OMX_FillThisBuffer failed with result %d\n", ret);
        }
        else {
            DEBUG_PRINT("OMX_FillThisBuffer success!\n");
        }
    }
    if(!tunnel)
    {
     DEBUG_PRINT("\nStart sending OMX_emptythisbuffer\n");
    for (i = 0;i < input_buf_cnt;i++) {
        DEBUG_PRINT ("\nOMX_EmptyThisBuffer on Input buf no.%d\n",i);
        pInputBufHdrs[i]->nInputPortIndex = 0;
        Size = Read_Buffer(pInputBufHdrs[i]);
        if(Size <=0 ){
          DEBUG_PRINT("\nNO DATA to READ\n");
          bInputEosReached = true;
          pInputBufHdrs[i]->nFlags |= OMX_BUFFERFLAG_EOS;
        }
        pInputBufHdrs[i]->nFilledLen = Size;
        pInputBufHdrs[i]->nInputPortIndex = 0;
        used_ip_buf_cnt++;
        ret = OMX_EmptyThisBuffer(qcelp13_enc_handle, pInputBufHdrs[i]);
        if (OMX_ErrorNone != ret) {
            DEBUG_PRINT("\nOMX_EmptyThisBuffer failed with result %d\n", ret);
        }
        else {
            DEBUG_PRINT("\nOMX_EmptyThisBuffer success!\n");
        }
        if(Size <=0 ){
            break;//eos reached
        }
    }
    pthread_mutex_lock(&etb_lock);
    if(etb_done)
    {
        DEBUG_PRINT("\nComponent is waiting for EBD to be released.\n");
        etb_event_complete();
    }
    else
    {
        DEBUG_PRINT("\n****************************\n");
        DEBUG_PRINT("EBD not yet happened ...\n");
        DEBUG_PRINT("\n****************************\n");
        etb_done++;
    }
    pthread_mutex_unlock(&etb_lock);
    }
    return 0;
}