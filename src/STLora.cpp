
#include <sx126x-hal.h>
#include "time.h"

#include "STLora.h"
#include "STHardware.h"
#include "SMScheduler.h"
extern SMScheduler sched;

int g_wait_delay=100;
extern RadioCallbacks_t RadioEvents =
{
    NULL,        // txDone
    NULL,        // rxDone
    NULL,             // rxPreambleDetect
    NULL,             // rxSyncWordDone
    NULL,             // rxHeaderDone
    NULL,     // txTimeout
    NULL,     // rxTimeout
    NULL,       // rxError
    NULL,       // cadDone
};

void STLora::setup(void)
{
ModulationParams_t ModulationParams;

  PacketStatus_t pktStatus;
  uint8_t Buffer[50];
  uint8_t BufferSize;
  int x;

  mode_rx=0;
  RadioNss=LORA_SS;
  TXEN = LORA_TXEN;
  RXEN = LORA_RXEN;

  for (x=0;x<20;x++)
     Buffer[x]=x;
  
  ModulationParams.PacketType = PACKET_TYPE_LORA;
  PacketParams.PacketType     = PACKET_TYPE_LORA;
  PacketParams.Params.LoRa.PreambleLength      =   8;                            
  PacketParams.Params.LoRa.HeaderType          = LORA_PACKET_EXPLICIT;
  PacketParams.Params.LoRa.PayloadLength       = 12;
  PacketParams.Params.LoRa.CrcMode             = LORA_CRC_ON;
  PacketParams.Params.LoRa.InvertIQ            = LORA_IQ_NORMAL;
  
  ModulationParams.Params.LoRa.SpreadingFactor = LORA_SF7;
  ModulationParams.Params.LoRa.Bandwidth       = LORA_BW_125;
  ModulationParams.Params.LoRa.CodingRate      = LORA_CR_4_5;

  RadioSpi = &SPI;


  pinMode(SS, OUTPUT);
  pinMode(TXEN,OUTPUT);
  pinMode(RXEN,OUTPUT);
  digitalWrite(TXEN,LOW);
  digitalWrite(RXEN,HIGH);
  Init();
  SetStandby( STDBY_XOSC );
  ClearIrqStatus( IRQ_RADIO_ALL );
  SetPacketType( ModulationParams.PacketType );
  SetModulationParams( &ModulationParams );
  SetPacketParams( &PacketParams );
  
  SetRfFrequency(915000000 );
  SetBufferBaseAddresses( 0x00, 0x00 );
  
  
  SetTxParams(22, RADIO_RAMP_200_US );
  SetRxTxFallbackMode(0x40);  //fallback to FS
  //LoRa.SetTxContinuousWave();
  //while(1);
  SetRegulatorMode(USE_DCDC);
  
  digitalWrite(TXEN,LOW);
  digitalWrite(RXEN,HIGH);
  SetDioIrqParams( IRQ_RX_DONE|IRQ_TX_DONE|IRQ_PREAMBLE_DETECTED|IRQ_CRC_ERROR, IRQ_RADIO_NONE, IRQ_RADIO_NONE, IRQ_RADIO_NONE );
  statRXGood=0;
  statRXBad=0;

}

void STLora::SetFrequency(int val)
{
  SetRfFrequency(val );
}
void STLora::SetPowerMode(uint8_t mode)
{
   SleepParams_t sleepConfig;
  // return;
  printf("Lore power mode setting %d\n",mode);
  sched.spi_take(2,10,"set sleep");
  if (mode ==POWER_OFF_RETAIN)
  {
        digitalWrite(TXEN,LOW);
        digitalWrite(RXEN,LOW);
        mode_rx=0;
        sleepConfig.Fields.WarmStart=1;
        SetSleep(sleepConfig);
  }
if (mode ==POWER_ON)
  {
        int attempts=3;

        mode_rx=0;
        //wake it up
        RadioNssSet(0);
        RadioNssSet(1);
        RadioNssSet(0);
        delay(1);
        SetStandby( STDBY_RC );
        digitalWrite(TXEN,LOW);
        digitalWrite(RXEN,HIGH);
        RadioStatus_t  rStat;

        while(attempts--)
        {
        rStat = GetStatus();
        if (rStat.Fields.ChipMode==2)
           break;
        }
        // make sure the radio woke up
        if (rStat.Fields.ChipMode!=2)
           printf("Error waking up  %d\n",rStat.Fields.ChipMode);
       // printf("After sleep mode = %d:stat %d\n",rStat.Fields.ChipMode,rStat.Fields.CmdStatus);
  }
  sched.spi_give();

}

uint8_t STLora::PacketDetected(void)
{
    uint8_t iStat;
    sched.spi_take(10,0,"Packet Detect");
    iStat = GetIrqStatus();
    sched.spi_give();
    if (iStat&IRQ_PREAMBLE_DETECTED)
       return 1;
    else
       return 0;

}
uint8_t STLora::ReceivePacket(LORA_PKT *pkt)
{
PacketStatus_t pktStatus;
    RadioStatus_t  rStat;

    uint8_t iStat,len;

     sched.spi_take(10,0,"Rx packet");
    rStat = GetStatus();
    iStat = GetIrqStatus();
   // printf ("istat %x rstat %x\n",iStat,rStat);
   // if (rStat.Fields.CmdStatus==2)
    if (iStat&IRQ_RX_DONE)
       {
        GetPayload((uint8_t *) pkt->Buf, &len, MAX_LORA_PKT );
        pkt->nSize=len;
        GetPacketStatus( &pktStatus );
        pkt->SNR = pktStatus.Params.LoRa.SnrPkt;
        pkt->rssi=pktStatus.Params.LoRa.RssiPkt;
       // printf("Got a packet  %x  %d\n",iStat,len);
        ClearIrqStatus(IRQ_RX_DONE|IRQ_PREAMBLE_DETECTED|IRQ_CRC_ERROR);
        sched.spi_give();
        if ((iStat&IRQ_CRC_ERROR)==0)
        {
           statRXGood++;
          return 1;
        }
        else 
        {
           printf("CRC error \n");
           statRXBad++;
          return 0;
        }
       }
    
     if (mode_rx==0)
      {
        int attempts=10;
        mode_rx=1;
        digitalWrite(TXEN,LOW);
        digitalWrite(RXEN,HIGH);
        SetRx(0xffffff);
//        printf("\nSwitching Mode \nstat %x %x irq %x\n",rStat.Fields.ChipMode,rStat.Fields.CmdStatus,iStat);
/* this needs to be investigated
        while (attempts--)
        {
        rStat = GetStatus();
         if (rStat.Fields.ChipMode==5)
            break;
          SetRx(0xffffff);
        }
       if (rStat.Fields.ChipMode!=5)
          printf("Status Error after setting r%x %x  %x\n",rStat.Fields.ChipMode,rStat.Fields.CmdStatus,iStat);
  */
      }
    sched.spi_give();
    return 0;
}
void  STLora::LoraPrepSend(LORA_PKT *pkt)
{
  int m=clock();
  //printf("Trying to get sem \n");
 sched.spi_take(10,0,"Prep");
 m=clock()-m;
// printf("--------Waited %d %s\n",m,sched.spi_last_owner);
}
uint8_t STLora::LoraSend(LORA_PKT *pkt)
{
    g_wait_delay=0;
    uint16_t iStat;
    digitalWrite(TXEN,HIGH);
    digitalWrite(RXEN,LOW);
  //  delay(400);
    //  LoRa.SetBufferBaseAddresses( 0x00, 0x00 );
     mode_rx=0;
     RadioStatus_t  rStat;


     PacketParams.Params.LoRa.PayloadLength       = pkt->nSize;
     SetPacketParams( &PacketParams );
     
     SetPayload( pkt->Buf, pkt->nSize);
     SetTx( 0 );
    
//     rStat = LoRa.GetStatus();
     iStat = GetIrqStatus();
     sched.spi_give();
//     while ((rStat.Fields.CmdStatus!=6))
    int to = 100;
     while ((iStat&IRQ_TX_DONE)==0)
         {
          delay(1);
          sched.spi_take(10,0,"tx stat");
          iStat = GetIrqStatus();
          sched.spi_give();
          to--;
          if(to==0)
             {
               printf("ERROR  Sending timeout\n");
               break;
             }
           //printf("wait to tx %x  %x\n",rStat.Fields.CmdStatus,iStat);
         }

    sched.spi_take(10,0,"tx done");

    ClearIrqStatus(IRQ_TX_DONE);
     
    digitalWrite(TXEN,LOW);
    digitalWrite(RXEN,HIGH);
    SetRx(0xffffff);
    mode_rx=1;
    sched.spi_give();
    return 1;
}



int get_toa(int n_size, int n_sf, int n_bw, int enable_eh, int enable_crc, int n_cr, int n_preamble)
{
    float r_sym = (n_bw*1000.) / pow(2,n_sf);
    float t_sym = 1000. / r_sym;
    float t_preamble = (n_preamble + 4.25) * t_sym;
    int v_CRC = 1;
    int v_IH = 0;
    int v_DE =0;
    /* ldre
    if enable_auto_ldro:
        if t_sym > 16:
            v_DE = 1
    elif enable_ldro:
        v_DE = 1
    # IH
    */
   #define max(a,b) (a>b?a:b)
    if (enable_eh==0)
        v_IH = 1;
    
    if (enable_crc==0)
        v_CRC = 0;
    float a = 8.*n_size - 4.*n_sf + 28 + 16*v_CRC - 20.*v_IH;
    float b = 4.*(n_sf-2.*v_DE);
    float v_ceil = a/b;
    float n_payload = 8 + max(ceil(a/b)*(n_cr+4), 0);
    float t_payload = n_payload * t_sym;
    float t_packet = t_preamble+ t_payload;
    return t_packet + .5;
}


// return the time to in ms to transmit the packet
int STLora::CalcPacketTime(int len)
{
 // return 0;
 int v;
 v= get_toa(len,7,125,1,1,1,8);
 //printf("\nCalc len %d t %d\n",len,v);
 return v;
  //return 21 + (len * 16) / 10;
}

uint8_t STLora::CalcMaxPacket(int time_ms)
{
  int len =8;
 // time_ms=160;
  printf("[CALC] time we have %d ms \n",time_ms);
  while (time_ms>CalcPacketTime(len))
    len++;
  printf("[CALC] Calced time = %d\n",len);
  return len-1;
}