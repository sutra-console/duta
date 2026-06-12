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
static void m_odesc(void*c,uint8_t i,uint8_t*t,const char**n){(void)c;(void)i;*t=SKRIT_CTRL_IO;*n="Relay";}
static uint16_t pwm[4];
static uint8_t m_pwmset(void*c,uint8_t i,uint16_t d){(void)c;if(i!=2)return 0;pwm[i]=d;return 1;} // only idx 2 PWMs
static uint16_t m_pwmget(void*c,uint8_t i){(void)c;return pwm[i];}
static uint32_t cfg_freq=1000; static uint8_t cfg_res=10;
static void m_pwmcfgget(void*c,uint8_t i,uint32_t*f,uint8_t*r){(void)c;if(i==2){*f=cfg_freq;*r=cfg_res;}else{*f=0;*r=0;}}
static uint8_t m_pwmcfgset(void*c,uint8_t i,uint32_t f,uint8_t r){(void)c;if(i!=2)return 0;if(f)cfg_freq=f;if(r)cfg_res=r;return 1;}
#define RGB_N 3 // mock rgb output (idx 2) has a 3-pixel strip
static uint8_t rgb[RGB_N][3];
static uint8_t m_rgbcount(void*c,uint8_t i){(void)c;return i==2?RGB_N:0;}
static uint8_t m_rgbset(void*c,uint8_t i,uint8_t px,uint8_t r,uint8_t g,uint8_t b){
  (void)c; if(i!=2)return 0;
  if(px==SKRIT_RGB_ALL){for(int p=0;p<RGB_N;p++){rgb[p][0]=r;rgb[p][1]=g;rgb[p][2]=b;}return 1;}
  if(px>=RGB_N)return 0;
  rgb[px][0]=r;rgb[px][1]=g;rgb[px][2]=b;return 1;}
static void m_rgbget(void*c,uint8_t i,uint8_t px,uint8_t*r,uint8_t*g,uint8_t*b){
  (void)c;(void)i; if(px>=RGB_N)px=0; *r=rgb[px][0];*g=rgb[px][1];*b=rgb[px][2];}
static uint32_t m_millis(void*c){(void)c;return fake_ms++;} // advances so waits terminate
// mock network auth: a mutable password store, factory default "duta"
static char g_pw[33]="duta"; static uint8_t g_pwlen=4, g_default=1;
static uint8_t m_authchk(void*c,const char*p,uint8_t n){(void)c;return n==g_pwlen && memcmp(p,g_pw,n)==0;}
static uint8_t m_authset(void*c,const char*p,uint8_t n){(void)c;if(n>32)return 0;memcpy(g_pw,p,n);g_pwlen=n;g_default=0;return 1;}
static uint8_t m_authdef(void*c){(void)c;return g_default;}
// mock provisioning: a 2-pin menu (pin 4 clean, pin 48 dual-use) + a 2-row table
static const int16_t prov_pin[2]={4,48}; static const uint8_t prov_warn[2]={0,1};
static const char* prov_name[2]={"","onboard WS2812"};
static uint8_t prov_set_called=0, prov_reset=0;
// mock key-value config: one rw "ssid" key + the masked wifi password
static char cfg_ssid[33]="lab"; static uint8_t cfg_ssid_n=3, cfg_pass_stored=1;
static int16_t m_kvget(void*c,uint8_t key,uint8_t*out,uint8_t cap){
  (void)c;
  if(key==SKRIT_CFG_WIFI_SSID){ if(cfg_ssid_n>cap)return -1; memcpy(out,cfg_ssid,cfg_ssid_n); return cfg_ssid_n; }
  if(key==SKRIT_CFG_WIFI_PASS){ if(!cfg_pass_stored)return 0; out[0]='*'; return 1; }
  return -1; }
static uint8_t m_kvset(void*c,uint8_t key,const uint8_t*val,uint8_t n){
  (void)c;
  if(key==SKRIT_CFG_WIFI_SSID){ if(n>32)return SKRIT_ST_BADARGS; memcpy(cfg_ssid,val,n); cfg_ssid_n=n; return SKRIT_ST_OK; }
  if(key==SKRIT_CFG_WIFI_PASS){ cfg_pass_stored=n>0; return SKRIT_ST_OK; }
  return SKRIT_ST_NOTFOUND; }
// mock I2C bus: one register device at 0x3C (reg pointer + 8-byte register file)
static uint8_t i2c_regs[8]={0x11,0x22,0x33,0,0,0,0,0}, i2c_ptr=0;
static uint8_t m_i2cscan(void*c,uint8_t bitmap[16]){(void)c;memset(bitmap,0,16);bitmap[0x3C>>3]|=1<<(0x3C&7);return SKRIT_ST_OK;}
static uint8_t m_i2cxfer(void*c,uint8_t addr,const uint8_t*w,uint8_t wlen,uint8_t*r,uint8_t rlen){
  (void)c; if(addr!=0x3C)return SKRIT_ST_NOTFOUND;
  for(uint8_t i=0;i<wlen;i++){ if(i==0)i2c_ptr=w[0]&7; else i2c_regs[(i2c_ptr+i-1)&7]=w[i]; }
  for(uint8_t i=0;i<rlen;i++) r[i]=i2c_regs[(i2c_ptr+i)&7];
  return SKRIT_ST_OK;}
static uint8_t m_pincaps(void*c,uint8_t i,int16_t*pin,uint8_t*caps,uint8_t*warn,uint8_t*bus,const char**nm){
  (void)c; if(i<2){*pin=prov_pin[i];*caps=SKRIT_PINCAP_DIGITAL|SKRIT_PINCAP_PWM;*warn=prov_warn[i];*bus=SKRIT_NO_BUS;*nm=prov_name[i];} return 2;}
static uint8_t m_cfgget(void*c,uint8_t i,uint8_t*t,int16_t*pin,uint8_t*fl,uint16_t*arg,const char**nm){
  (void)c;
  if(i==0){*t=SKRIT_CTRL_IO;*pin=4;*fl=0;*arg=0;*nm="Relay";}
  else if(i==1){*t=SKRIT_CTRL_PWM;*pin=5;*fl=0;*arg=0;*nm="LED";}
  return 2;}
static uint8_t m_cfgset(void*c,const uint8_t*body,uint8_t len,uint8_t*bad){
  (void)c;(void)len; uint8_t n=body[0];
  if(n==SKRIT_CONFIG_RESET){prov_reset=1;return SKRIT_ST_OK;}
  const uint8_t*p=body+1; // each row: type(1),pin(2),flags(1),arg(2),namelen(1),name
  for(uint8_t k=0;k<n;k++){ int16_t pin=(int16_t)(p[1]|(p[2]<<8));
    if(pin!=4&&pin!=48){*bad=k;return SKRIT_ST_BADARGS;} p+=7+p[6]; }
  prov_set_called=1; return SKRIT_ST_OK;}

// mock INVOKE: a well-known set_position(x:u16,y:u16) + a vendor echo(bytes)->reply
static uint16_t inv_x, inv_y; static uint8_t inv_called;
static uint8_t m_cmddesc(void*c,uint8_t i,uint16_t*id,uint8_t*na,uint8_t*at,uint8_t*fl,const char**nm){
  (void)c;
  if(i==0){*id=SKRIT_INVOKE_SET_POSITION;*na=2;at[0]=SKRIT_ARG_U16;at[1]=SKRIT_ARG_U16;*fl=0;*nm="set_position";}
  else if(i==1){*id=SKRIT_INVOKE_VENDOR_BASE+1;*na=1;at[0]=SKRIT_ARG_BYTES;*fl=SKRIT_INVOKE_REPLY;*nm="echo";}
  return 2;}
static uint8_t m_cmdinvoke(void*c,uint16_t id,const uint8_t*p,uint8_t pl,uint8_t*rep,uint8_t cap,uint8_t*rl){
  (void)c; *rl=0;
  if(id==SKRIT_INVOKE_SET_POSITION){ if(pl<4)return SKRIT_ST_BADARGS;
    inv_x=(uint16_t)(p[0]|(p[1]<<8)); inv_y=(uint16_t)(p[2]|(p[3]<<8)); inv_called=1; return SKRIT_ST_OK; }
  if(id==SKRIT_INVOKE_VENDOR_BASE+1){ // echo: payload = len(1),bytes -> reply those bytes
    if(pl<1)return SKRIT_ST_BADARGS;
    uint8_t n=p[0];
    if((uint16_t)n+1>pl)return SKRIT_ST_BADARGS;
    if(n>cap)n=cap;
    memcpy(rep,p+1,n); *rl=n; return SKRIT_ST_OK; }
  return SKRIT_ST_NOTFOUND;}

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
  hal.pwm_config_get=m_pwmcfgget;hal.pwm_config_set=m_pwmcfgset;
  hal.rgb_count=m_rgbcount;hal.rgb_set=m_rgbset;hal.rgb_get=m_rgbget;

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

  // ---- PWM_CONFIG: read defaults, set freq/res, reject a non-pwm output ----
  { uint8_t g[1]={2}; link_n=0; feed_cmd(&dev,SKRIT_PWM_CONFIG,40,g,1,1); last_resp(r,1);
    uint32_t f = r[5]|(r[6]<<8)|(r[7]<<16)|((uint32_t)r[8]<<24);
    assert(r[3]==SKRIT_ST_OK && r[4]==2 && f==1000 && r[9]==10);
    uint8_t s[6]={2, 0x88,0x13,0,0, 8}; // freq 5000 (0x1388 LE), res 8
    link_n=0; feed_cmd(&dev,SKRIT_PWM_CONFIG,41,s,6,1); last_resp(r,1);
    f = r[5]|(r[6]<<8)|(r[7]<<16)|((uint32_t)r[8]<<24);
    assert(r[3]==SKRIT_ST_OK && f==5000 && r[9]==8);
    uint8_t bad[6]={0, 0x88,0x13,0,0, 8}; // a relay isn't pwm -> BADARGS
    link_n=0; feed_cmd(&dev,SKRIT_PWM_CONFIG,42,bad,6,1); last_resp(r,1);
    assert(r[3]==SKRIT_ST_BADARGS); }
  printf("PWM_CONFIG get/set/reject ok\n");

  // ---- OUT_RGB: fill all, read back (with count), set one pixel, reject relay ----
  { uint8_t fill[4]={2, 0x12, 0x34, 0x56}; // len 4 = fill all pixels
    link_n=0; feed_cmd(&dev,SKRIT_OUT_RGB,17,fill,4,1); rn=last_resp(r,1);
    // resp: status,index,count,r,g,b
    assert(r[3]==SKRIT_ST_OK && r[4]==2 && r[5]==RGB_N && r[6]==0x12 && r[7]==0x34 && r[8]==0x56);
    assert(rgb[0][0]==0x12 && rgb[1][0]==0x12 && rgb[2][0]==0x12); // all filled
    uint8_t setpx[5]={2, 1, 0xAA, 0xBB, 0xCC}; // len 5 = set pixel 1
    link_n=0; feed_cmd(&dev,SKRIT_OUT_RGB,18,setpx,5,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && rgb[1][0]==0xAA && rgb[1][1]==0xBB && rgb[1][2]==0xCC);
    assert(rgb[0][0]==0x12); // pixel 0 untouched
    uint8_t gc[1]={2}; link_n=0; feed_cmd(&dev,SKRIT_OUT_RGB,19,gc,1,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && r[5]==RGB_N && r[6]==0x12); // read = count + pixel 0
    uint8_t badc[4]={0, 1, 2, 3}; link_n=0; feed_cmd(&dev,SKRIT_OUT_RGB,20,badc,4,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_BADARGS); } // relay isn't rgb
  printf("OUT_RGB fill/setpx/read/reject ok\n");

  // ---- scratch macro with SETRGB opcode (fills the strip) ----
  { uint8_t prog3[]={SKRIT_MC_VER, SKRIT_MC_SETRGB,2,0x01,0x02,0x03, SKRIT_MC_END};
    uint8_t beg3[3]={SKRIT_MC_SCRATCH,(uint8_t)sizeof prog3,0};
    feed_cmd(&dev,SKRIT_MACRO_WRITE_BEGIN,21,beg3,3,1);
    uint8_t wd3[3+sizeof prog3]; wd3[0]=SKRIT_MC_SCRATCH;wd3[1]=0;wd3[2]=0;memcpy(wd3+3,prog3,sizeof prog3);
    feed_cmd(&dev,SKRIT_MACRO_WRITE_DATA,22,wd3,3+sizeof prog3,1);
    uint8_t en5[1]={SKRIT_MC_SCRATCH}; feed_cmd(&dev,SKRIT_MACRO_WRITE_END,23,en5,1,1);
    memset(rgb,0,sizeof rgb); uint8_t run3[1]={SKRIT_MC_SCRATCH};
    link_n=0; feed_cmd(&dev,SKRIT_MACRO_RUN,24,run3,1,1); rn=last_resp(r,1);
    assert(r[3]==SKRIT_ST_OK && rgb[0][0]==0x01 && rgb[2][2]==0x03); } // whole strip filled
  printf("macro SETRGB ok\n");

  // ---- DATA_DESC: default UART, and a CAN device ----
  link_n = 0; feed_cmd(&dev, SKRIT_DATA_DESC, 30, NULL, 0, 1); last_resp(r, 1);
  assert(r[3] == SKRIT_ST_OK && r[4] == SKRIT_DATA_UART &&
         r[5] == 'U' && r[6] == 'A' && r[7] == 'R' && r[8] == 'T');
  {
    skrit_hal chal = hal;
    chal.data_kind = SKRIT_DATA_CAN;
    skrit_dev cdev;
    skrit_dev_init(&cdev, &chal, NULL, 1);
    link_n = 0; feed_cmd(&cdev, SKRIT_DATA_DESC, 31, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && r[4] == SKRIT_DATA_CAN && r[5] == 'C' && r[6] == 'A' && r[7] == 'N');
  }
  printf("DATA_DESC uart/can ok\n");

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

  // ---- AUTH: a network-gated device ----
  {
    skrit_hal ahal = hal; // copy the base mock + add auth
    ahal.auth_required = 1;
    ahal.auth_check = m_authchk;
    ahal.auth_set = m_authset;
    ahal.auth_is_default = m_authdef;
    skrit_dev adev;
    skrit_dev_init(&adev, &ahal, NULL, 1);

    // INFO advertises auth-required + default-credential (flags = body[9] = r[12])
    link_n = 0; feed_cmd(&adev, SKRIT_INFO, 1, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && r[12] == (SKRIT_FLAG_AUTH_REQUIRED | SKRIT_FLAG_DEFAULT_CRED));
    // gated: a normal CMD is rejected until AUTH
    link_n = 0; feed_cmd(&adev, SKRIT_OUT_GET, 2, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_UNAUTH);
    // wrong password
    uint8_t bad[2] = {'n', 'o'}; link_n = 0; feed_cmd(&adev, SKRIT_AUTH, 3, bad, 2, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_UNAUTH);
    // correct password "duta"
    uint8_t pw[4] = {'d', 'u', 't', 'a'}; link_n = 0; feed_cmd(&adev, SKRIT_AUTH, 4, pw, 4, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK);
    // now a normal CMD works
    link_n = 0; feed_cmd(&adev, SKRIT_OUT_GET, 5, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK);
    // change the password
    uint8_t np[3] = {'s', '3', 'c'}; link_n = 0; feed_cmd(&adev, SKRIT_AUTH_SET, 6, np, 3, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && g_pwlen == 3 && g_default == 0);
    // reset auth (new connection) -> gated again; INFO no longer flags default-cred
    skrit_dev_reset_auth(&adev);
    link_n = 0; feed_cmd(&adev, SKRIT_OUT_GET, 7, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_UNAUTH);
    link_n = 0; feed_cmd(&adev, SKRIT_INFO, 8, NULL, 0, 1); last_resp(r, 1);
    assert((r[12] & SKRIT_FLAG_DEFAULT_CRED) == 0);
  }
  printf("AUTH gate/login/set/reset ok\n");

  // ---- runtime provisioning: PIN_CAPS menu, CONFIG_GET table, CONFIG_SET ----
  {
    skrit_hal phal = hal;
    phal.pin_caps = m_pincaps; phal.config_get = m_cfgget; phal.config_set = m_cfgset;
    skrit_dev pdev; skrit_dev_init(&pdev, &phal, NULL, 1);

    // INFO advertises the provision flag (flags = body[9] = r[12])
    link_n = 0; feed_cmd(&pdev, SKRIT_INFO, 1, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && (r[12] & SKRIT_FLAG_PROVISION));
    // PIN_CAPS[0]: total 2, pin 4, caps digital|pwm, clean. body: st,idx,total,pinlo,pinhi,caps,warn,bus
    { uint8_t a[1] = {0}; link_n = 0; feed_cmd(&pdev, SKRIT_PIN_CAPS, 2, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[4] == 0 && r[5] == 2);
      assert(r[6] == 4 && r[7] == 0 && r[8] == (SKRIT_PINCAP_DIGITAL | SKRIT_PINCAP_PWM));
      assert(r[9] == SKRIT_PIN_CLEAN && r[10] == SKRIT_NO_BUS); }
    // PIN_CAPS[1]: dual-use -> warn + reason name "onboard WS2812" (name at r[11])
    { uint8_t a[1] = {1}; link_n = 0; feed_cmd(&pdev, SKRIT_PIN_CAPS, 3, a, 1, 1); last_resp(r, 1);
      assert(r[6] == 48 && r[9] == SKRIT_PIN_WARN && r[11] == 'o'); }
    // PIN_CAPS past the end: just st,index,total (no tuple) -> LEN (r[2]) == 3
    { uint8_t a[1] = {2}; link_n = 0; feed_cmd(&pdev, SKRIT_PIN_CAPS, 4, a, 1, 1); last_resp(r, 1);
      assert(r[2] == 3 && r[4] == 2 && r[5] == 2); }
    // CONFIG_GET[0]: type IO, pin 4, name "Relay". body: st,idx,n,type,pinlo,pinhi,flags,arglo,arghi,name
    { uint8_t a[1] = {0}; link_n = 0; feed_cmd(&pdev, SKRIT_CONFIG_GET, 5, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[4] == 0 && r[5] == 2 && r[6] == SKRIT_CTRL_IO && r[7] == 4 && r[12] == 'R'); }
    // CONFIG_SET a valid 1-row table {IO, pin 4, "X"} -> OK
    { uint8_t row[] = {1, SKRIT_CTRL_IO, 4, 0, 0, 0, 0, 1, 'X'}; link_n = 0;
      feed_cmd(&pdev, SKRIT_CONFIG_SET, 6, row, (uint8_t)sizeof row, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && prov_set_called == 1); }
    // CONFIG_SET an off-menu pin (99) -> BADARGS + bad_index 0
    { uint8_t row[] = {1, SKRIT_CTRL_IO, 99, 0, 0, 0, 0, 0}; link_n = 0;
      feed_cmd(&pdev, SKRIT_CONFIG_SET, 7, row, (uint8_t)sizeof row, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_BADARGS && r[4] == 0); }
    // CONFIG_SET reset sentinel -> OK + reset path taken
    { uint8_t row[1] = {SKRIT_CONFIG_RESET}; link_n = 0;
      feed_cmd(&pdev, SKRIT_CONFIG_SET, 8, row, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && prov_reset == 1); }
    // a device without the provision callbacks answers UNSUPPORTED
    skrit_dev ndev; skrit_dev_init(&ndev, &hal, NULL, 1);
    { uint8_t a[1] = {0}; link_n = 0; feed_cmd(&ndev, SKRIT_PIN_CAPS, 9, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_UNSUPPORTED); }
  }
  printf("provisioning PIN_CAPS/CONFIG_GET/SET ok\n");

  // ---- CFG_GET/CFG_SET: key-value config (WiFi keys) ----
  {
    // base mock has no cfg callbacks -> UNSUPPORTED
    skrit_dev ndev2; skrit_dev_init(&ndev2, &hal, NULL, 1);
    { uint8_t a[1] = {SKRIT_CFG_WIFI_SSID}; link_n = 0; feed_cmd(&ndev2, SKRIT_CFG_GET, 1, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_UNSUPPORTED); }
    skrit_hal khal = hal;
    khal.cfg_get = m_kvget; khal.cfg_set = m_kvset;
    skrit_dev kdev; skrit_dev_init(&kdev, &khal, NULL, 1);
    // GET ssid -> "lab" (body: st, key, value...)
    { uint8_t a[1] = {SKRIT_CFG_WIFI_SSID}; link_n = 0; feed_cmd(&kdev, SKRIT_CFG_GET, 2, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[4] == SKRIT_CFG_WIFI_SSID && r[5] == 'l' && r[6] == 'a' && r[7] == 'b'); }
    // SET ssid -> stored
    { uint8_t s[] = {SKRIT_CFG_WIFI_SSID, 'h', 'o', 'm', 'e'}; link_n = 0;
      feed_cmd(&kdev, SKRIT_CFG_SET, 3, s, (uint8_t)sizeof s, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && cfg_ssid_n == 4 && cfg_ssid[0] == 'h'); }
    // GET password -> masked "*", never the secret
    { uint8_t a[1] = {SKRIT_CFG_WIFI_PASS}; link_n = 0; feed_cmd(&kdev, SKRIT_CFG_GET, 4, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[5] == '*' && r[2] == 3); }
    // unknown key -> NOTFOUND
    { uint8_t a[1] = {0x77}; link_n = 0; feed_cmd(&kdev, SKRIT_CFG_GET, 5, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_NOTFOUND); }
  }
  printf("CFG get/set (wifi keys) ok\n");

  // ---- I2C master: scan bitmap + write/read transfer ----
  {
    // no callbacks -> UNSUPPORTED
    skrit_dev bare; skrit_dev_init(&bare, &hal, NULL, 1);
    link_n = 0; feed_cmd(&bare, SKRIT_I2C_SCAN, 1, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_UNSUPPORTED);

    skrit_hal ihal = hal;
    ihal.i2c_scan = m_i2cscan; ihal.i2c_xfer = m_i2cxfer;
    skrit_dev idev; skrit_dev_init(&idev, &ihal, NULL, 1);
    // scan -> bitmap with only 0x3C set
    link_n = 0; feed_cmd(&idev, SKRIT_I2C_SCAN, 2, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && r[2] == 17);
    assert(r[4 + (0x3C >> 3)] == (1 << (0x3C & 7)));
    for (int i = 0; i < 16; i++) if (i != (0x3C >> 3)) assert(r[4 + i] == 0);
    // write reg pointer 1 + two bytes, then read them back
    { uint8_t x[] = {0x3C, 3, 0x01, 0xAA, 0xBB, 0}; // addr, wlen=3, ptr+2 data, rlen=0
      link_n = 0; feed_cmd(&idev, SKRIT_I2C_XFER, 3, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[4] == 0x3C); }
    { uint8_t x[] = {0x3C, 1, 0x01, 2}; // set ptr=1, read 2
      link_n = 0; feed_cmd(&idev, SKRIT_I2C_XFER, 4, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[5] == 0xAA && r[6] == 0xBB); }
    // NAK (absent address) -> NOTFOUND
    { uint8_t x[] = {0x10, 0, 1}; link_n = 0;
      feed_cmd(&idev, SKRIT_I2C_XFER, 5, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_NOTFOUND); }
    // malformed (wlen doesn't match body) -> BADARGS
    { uint8_t x[] = {0x3C, 9, 0x01, 2}; link_n = 0;
      feed_cmd(&idev, SKRIT_I2C_XFER, 6, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_BADARGS); }
  }
  printf("I2C scan/xfer ok\n");

  // ---- INVOKE: user-defined commands (desc + call + reply + macro op) ----
  {
    // no callbacks -> UNSUPPORTED
    skrit_dev bare; skrit_dev_init(&bare, &hal, NULL, 1);
    { uint8_t a[1] = {0}; link_n = 0; feed_cmd(&bare, SKRIT_INVOKE_DESC, 1, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_UNSUPPORTED); }

    skrit_hal vhal = hal; vhal.cmd_desc = m_cmddesc; vhal.cmd_invoke = m_cmdinvoke;
    skrit_dev vdev; skrit_dev_init(&vdev, &vhal, NULL, 1);
    // INFO advertises FLAG_INVOKE (flags = body[9] = r[12])
    link_n = 0; feed_cmd(&vdev, SKRIT_INFO, 2, NULL, 0, 1); last_resp(r, 1);
    assert(r[3] == SKRIT_ST_OK && (r[12] & SKRIT_FLAG_INVOKE));
    // INVOKE_DESC[0] = set_position(u16,u16), no reply
    { uint8_t a[1] = {0}; link_n = 0; feed_cmd(&vdev, SKRIT_INVOKE_DESC, 3, a, 1, 1); last_resp(r, 1);
      // r: TYPE,SEQ,LEN, ST,index,total, id_lo,id_hi, nargs, at0,at1, flags, name...
      assert(r[3] == SKRIT_ST_OK && r[4] == 0 && r[5] == 2);
      assert(r[6] == (SKRIT_INVOKE_SET_POSITION & 0xFF) && r[7] == (SKRIT_INVOKE_SET_POSITION >> 8));
      assert(r[8] == 2 && r[9] == SKRIT_ARG_U16 && r[10] == SKRIT_ARG_U16 && r[11] == 0); }
    // INVOKE_DESC[1] = vendor echo (0x8001), REPLY flag, one bytes arg
    { uint8_t a[1] = {1}; link_n = 0; feed_cmd(&vdev, SKRIT_INVOKE_DESC, 4, a, 1, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[6] == 0x01 && r[7] == 0x80);
      assert(r[8] == 1 && r[9] == SKRIT_ARG_BYTES && r[10] == SKRIT_INVOKE_REPLY); }
    // INVOKE set_position(100,200): id(2 LE) + x(2) + y(2)
    { uint8_t x[] = {SKRIT_INVOKE_SET_POSITION & 0xFF, SKRIT_INVOKE_SET_POSITION >> 8, 100, 0, 200, 0};
      inv_called = 0; link_n = 0; feed_cmd(&vdev, SKRIT_INVOKE, 5, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[4] == (SKRIT_INVOKE_SET_POSITION & 0xFF) && r[5] == (SKRIT_INVOKE_SET_POSITION >> 8));
      assert(inv_called && inv_x == 100 && inv_y == 200); }
    // INVOKE vendor echo -> status, id(2), reply bytes "abc"
    { uint16_t id = SKRIT_INVOKE_VENDOR_BASE + 1;
      uint8_t x[] = {(uint8_t)(id & 0xFF), (uint8_t)(id >> 8), 3, 'a', 'b', 'c'};
      link_n = 0; feed_cmd(&vdev, SKRIT_INVOKE, 6, x, (uint8_t)sizeof x, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_OK && r[6] == 'a' && r[7] == 'b' && r[8] == 'c'); }
    // unknown id -> NOTFOUND
    { uint8_t x[] = {0x99, 0x99}; link_n = 0; feed_cmd(&vdev, SKRIT_INVOKE, 7, x, 2, 1); last_resp(r, 1);
      assert(r[3] == SKRIT_ST_NOTFOUND); }
    // the skrit-mc INVOKE opcode drives the same handler
    { inv_called = 0;
      uint8_t prog[] = {SKRIT_MC_VER, SKRIT_MC_INVOKE,
                        SKRIT_INVOKE_SET_POSITION & 0xFF, SKRIT_INVOKE_SET_POSITION >> 8, 4, 7, 0, 9, 0, SKRIT_MC_END};
      uint8_t st = skrit__run_program(&vdev, prog, (uint16_t)sizeof prog);
      assert(st == SKRIT_ST_OK && inv_called && inv_x == 7 && inv_y == 9); }
  }
  printf("INVOKE desc/call/reply/macro ok\n");

  // ---- skrit_dev_feed_data: one read tees to several links ----
  {
    skrit_dev d1, d2;
    skrit_dev_init(&d1, &hal, NULL, 1);
    skrit_dev_init(&d2, &hal, NULL, 1);
    link_n = 0;
    skrit_dev_feed_data(&d1, (const uint8_t *)"hi", 2);
    skrit_dev_feed_data(&d2, (const uint8_t *)"hi", 2);
    // both wrapped the bytes onto the (shared mock) link as DATA frames
    int end = link_n; int frames = 0;
    for (int i = 0; i < end; i++) if (link_out[i] == 0) frames++; // delimiters
    assert(link_n > 0 && frames >= 2);
  }
  printf("feed_data multi-link tee ok\n");

  printf("ALL CORE TESTS PASSED\n");
  return 0;
}
