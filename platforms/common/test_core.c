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

  printf("ALL CORE TESTS PASSED\n");
  return 0;
}
