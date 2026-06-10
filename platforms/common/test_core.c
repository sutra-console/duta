#include <stdio.h>
#include <string.h>
#include <assert.h>

#define SKRIT_SCRATCH_CAP 512
#include "skrit_device.h"

// ---- mock platform ----
static uint8_t link_out[4096]; static int link_n;
static uint8_t target_out[4096]; static int target_n;        // EMIT / host->target
static uint8_t console_in[4096]; static int console_n, console_pos; // target->host queue
static uint8_t outs[4];
static uint32_t fake_ms;

static void m_link(void*c,const uint8_t*p,uint16_t n){(void)c;memcpy(link_out+link_n,p,n);link_n+=n;}
static void m_data(void*c,const uint8_t*p,uint16_t n){(void)c;memcpy(target_out+target_n,p,n);target_n+=n;}
static uint16_t m_read(void*c,uint8_t*o,uint16_t cap){(void)c;uint16_t k=0;while(console_pos<console_n&&k<cap)o[k++]=console_in[console_pos++];return k;}
static void m_oset(void*c,uint8_t i,uint8_t on){(void)c;outs[i]=on;}
static uint8_t m_oget(void*c,uint8_t i){(void)c;return outs[i];}
static void m_odesc(void*c,uint8_t i,uint8_t*t,const char**n){(void)c;(void)i;*t=SKRIT_CTRL_RELAY;*n="Relay";}
static uint16_t pwm[4];
static uint8_t m_pwmset(void*c,uint8_t i,uint16_t d){(void)c;if(i!=2)return 0;pwm[i]=d;return 1;} // only idx 2 PWMs
static uint16_t m_pwmget(void*c,uint8_t i){(void)c;return pwm[i];}
static uint32_t m_millis(void*c){(void)c;return fake_ms++;} // advances so waits terminate

static skrit_hal hal = {0};
static skrit_dev dev;

// frame a CMD request the way the host would, feed it to the device
static void feed_cmd(skrit_dev*d,uint8_t type,uint8_t seq,const uint8_t*body,uint8_t bl,int muxed){
  uint8_t raw[80]; raw[0]=type;raw[1]=seq;raw[2]=bl; if(bl)memcpy(raw+3,body,bl);
  raw[3+bl]=skrit_crc8(raw,3+bl);
  uint8_t payload[81]; int plen;
  if(muxed){payload[0]=SKRIT_MUX_CMD;memcpy(payload+1,raw,4+bl);plen=5+bl;}
  else {memcpy(payload,raw,4+bl);plen=4+bl;}
  uint8_t cobs[100]; size_t en=skrit_cobs_encode(payload,plen,cobs);
  skrit_dev_rx(d,0x00); for(size_t i=0;i<en;i++)skrit_dev_rx(d,cobs[i]); skrit_dev_rx(d,0x00);
}
// decode the last frame the device sent on link_out (strip delimiters+COBS[+mux])
static int last_resp(uint8_t*out,int muxed){
  // find last non-empty segment between 0x00s
  int end=link_n; while(end>0&&link_out[end-1]==0)end--;
  int start=end; while(start>0&&link_out[start-1]!=0)start--;
  if(end<=start)return -1;
  uint8_t dec[100]; size_t dn=skrit_cobs_decode(link_out+start,end-start,dec);
  int off=muxed?1:0; memcpy(out,dec+off,dn-off); return (int)dn-off;
}

int main(void){
  hal.name="Duta test";hal.fw_ver=0x0003;hal.caps=SKRIT_CAP_MUX;hal.macro_tier=2;
  hal.n_outputs=3;hal.n_inputs=0;
  hal.link_write=m_link;hal.data_write=m_data;hal.data_read=m_read;
  hal.out_set=m_oset;hal.out_get=m_oget;hal.out_desc=m_odesc;hal.millis=m_millis;
  hal.pwm_set=m_pwmset;hal.pwm_get=m_pwmget;

  // ---- MUX: PING ----
  skrit_dev_init(&dev,&hal,NULL,1);
  link_n=0; feed_cmd(&dev,SKRIT_PING,0x11,NULL,0,1);
  uint8_t r[100]; int rn=last_resp(r,1);
  assert(rn==4+5); assert(r[0]==(SKRIT_PING|SKRIT_RESP)); assert(r[1]==0x11);
  assert(r[3]==SKRIT_ST_OK && r[4]=='P'&&r[5]=='O'&&r[6]=='N'&&r[7]=='G');
  printf("mux PING ok\n");

  // ---- MUX: INFO carries caps/tier/proto ----
  link_n=0; feed_cmd(&dev,SKRIT_INFO,1,NULL,0,1); rn=last_resp(r,1);
  assert(r[3]==SKRIT_ST_OK); // body: st,fwlo,fwhi,caps,nout,store,proto,nin,tier
  assert(r[4]==0x03&&r[5]==0x00); assert(r[6]==SKRIT_CAP_MUX); assert(r[7]==3);
  assert(r[9]==SKRIT_PROTO_VER); assert(r[11]==2);
  printf("mux INFO ok (caps=%02x proto=%d tier=%d)\n",r[6],r[9],r[11]);

  // ---- MUX: OUTPUT_SET then OUTPUT_GET bitmap ----
  uint8_t sb[2]={1,1}; link_n=0; feed_cmd(&dev,SKRIT_OUT_SET,2,sb,2,1);
  assert(outs[1]==1);
  link_n=0; feed_cmd(&dev,SKRIT_OUT_GET,3,NULL,0,1); rn=last_resp(r,1);
  assert(r[3]==SKRIT_ST_OK && r[4]==0x02); // bit1 set
  printf("mux OUTPUT set/get ok (bitmap=%d)\n",r[4]);

  // ---- MUX: host->target DATA channel passes through to UART ----
  target_n=0;
  { uint8_t pl[4]={SKRIT_MUX_DATA,'h','i','\n'}; uint8_t cobs[16]; size_t en=skrit_cobs_encode(pl,4,cobs);
    skrit_dev_rx(&dev,0); for(size_t i=0;i<en;i++)skrit_dev_rx(&dev,cobs[i]); skrit_dev_rx(&dev,0); }
  assert(target_n==3 && target_out[0]=='h'&&target_out[2]=='\n');
  printf("mux DATA host->target ok\n");

  // ---- MUX: target->host console gets wrapped on the link ----
  console_n=0;console_pos=0; const char*line="ready>"; memcpy(console_in,line,6);console_n=6;
  link_n=0; skrit_dev_poll(&dev);
  { int end=link_n; while(end>0&&link_out[end-1]==0)end--; int s=end; while(s>0&&link_out[s-1]!=0)s--;
    uint8_t dec[64]; size_t dn=skrit_cobs_decode(link_out+s,end-s,dec);
    assert(dec[0]==SKRIT_MUX_DATA && dn==7 && dec[1]=='r'&&dec[6]=='>'); }
  printf("mux DATA target->host ok\n");

  // ---- scratch macro: EMIT "AT\r" ; DELAY 1 ; SETOUT 0 1 ; END  then RUN ----
  target_n=0;
  uint8_t prog[]={SKRIT_MC_VER, SKRIT_MC_EMIT,3,'A','T','\r', SKRIT_MC_DELAY,1,0, SKRIT_MC_SETOUT,0,1, SKRIT_MC_END};
  uint8_t beg[3]={SKRIT_MC_SCRATCH,(uint8_t)sizeof prog,0};
  feed_cmd(&dev,SKRIT_MACRO_WRITE_BEGIN,4,beg,3,1);
  uint8_t wd[3+sizeof prog]; wd[0]=SKRIT_MC_SCRATCH;wd[1]=0;wd[2]=0;memcpy(wd+3,prog,sizeof prog);
  feed_cmd(&dev,SKRIT_MACRO_WRITE_DATA,5,wd,3+sizeof prog,1);
  uint8_t en2[1]={SKRIT_MC_SCRATCH}; feed_cmd(&dev,SKRIT_MACRO_WRITE_END,6,en2,1,1);
  link_n=0; outs[0]=0; uint8_t run[1]={SKRIT_MC_SCRATCH}; feed_cmd(&dev,SKRIT_MACRO_RUN,7,run,1,1);
  rn=last_resp(r,1);
  assert(r[0]==(SKRIT_MACRO_RUN|SKRIT_RESP)&&r[3]==SKRIT_ST_OK);
  assert(target_n==3 && target_out[0]=='A'&&target_out[1]=='T'&&target_out[2]=='\r');
  assert(outs[0]==1);
  printf("scratch macro EMIT/DELAY/SETOUT/RUN ok\n");

  // ---- PWM: set duty on the PWM output, read it back, reject a relay ----
  { uint8_t sp[3]={2, 0x00, 0x02}; // idx 2, duty 512
    link_n=0; feed_cmd(&dev,SKRIT_OUT_PWM,10,sp,3,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && r[4]==2 && (r[5]|(r[6]<<8))==512 && pwm[2]==512);
    uint8_t gp[1]={2}; link_n=0; feed_cmd(&dev,SKRIT_OUT_PWM,11,gp,1,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && (r[5]|(r[6]<<8))==512);
    uint8_t bad[3]={0, 0x00, 0x02}; // a relay can't PWM -> BADARGS
    link_n=0; feed_cmd(&dev,SKRIT_OUT_PWM,12,bad,3,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_BADARGS); }
  printf("OUT_PWM set/get/reject ok\n");

  // ---- scratch macro with SETPWM opcode ----
  { uint8_t prog2[]={SKRIT_MC_VER, SKRIT_MC_SETPWM,2,0xFF,0x03, SKRIT_MC_END}; // duty 1023
    uint8_t beg2[3]={SKRIT_MC_SCRATCH,(uint8_t)sizeof prog2,0};
    feed_cmd(&dev,SKRIT_MACRO_WRITE_BEGIN,13,beg2,3,1);
    uint8_t wd2[3+sizeof prog2]; wd2[0]=SKRIT_MC_SCRATCH;wd2[1]=0;wd2[2]=0;memcpy(wd2+3,prog2,sizeof prog2);
    feed_cmd(&dev,SKRIT_MACRO_WRITE_DATA,14,wd2,3+sizeof prog2,1);
    uint8_t en4[1]={SKRIT_MC_SCRATCH}; feed_cmd(&dev,SKRIT_MACRO_WRITE_END,15,en4,1,1);
    pwm[2]=0; uint8_t run2[1]={SKRIT_MC_SCRATCH};
    link_n=0; feed_cmd(&dev,SKRIT_MACRO_RUN,16,run2,1,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && pwm[2]==1023); }
  printf("macro SETPWM ok\n");

  // ---- DUAL link: PING is a bare COBS frame (no channel tag) ----
  skrit_dev_init(&dev,&hal,NULL,0);
  link_n=0; feed_cmd(&dev,SKRIT_PING,9,NULL,0,0); rn=last_resp(r,0);
  assert(r[0]==(SKRIT_PING|SKRIT_RESP)&&r[1]==9&&r[3]==SKRIT_ST_OK);
  printf("dual PING ok\n");

  // ---- bad CRC -> BADCRC status ----
  { uint8_t raw[4]={SKRIT_PING,3,0,0x00 /*wrong crc*/}; uint8_t cobs[8]; size_t en3=skrit_cobs_encode(raw,4,cobs);
    link_n=0; skrit_dev_rx(&dev,0); for(size_t i=0;i<en3;i++)skrit_dev_rx(&dev,cobs[i]); skrit_dev_rx(&dev,0);
    rn=last_resp(r,0); assert(r[3]==SKRIT_ST_BADCRC); }
  printf("dual BADCRC ok\n");

  printf("ALL CORE TESTS PASSED\n");
  return 0;
}
