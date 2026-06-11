#include "ws_codec.h"
#include <assert.h>
#include <stdio.h>
int main(void){
  // RFC 6455 §1.3 example: key -> accept
  char acc[40]; ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", acc);
  printf("accept=%s\n", acc);
  assert(strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

  // SHA1("abc") base64 sanity via known digest a9993e36... (check first bytes)
  // masked client frame round-trip: encode unmasked, then build a masked frame
  // and decode it.
  uint8_t payload[] = {0x00, 0x41, 0x42, 0x00}; // mux-ish bytes incl zeros
  uint8_t masked[32]; size_t mo=0;
  masked[mo++] = 0x82;            // FIN + binary
  masked[mo++] = 0x80 | 4;        // masked, len 4
  uint8_t key[4] = {0x37,0xfa,0x21,0x3d};
  memcpy(masked+mo, key, 4); mo+=4;
  for (int i=0;i<4;i++) masked[mo++] = payload[i]^key[i&3];
  uint8_t out[32]; size_t olen; uint8_t op;
  long used = ws_frame_decode(masked, mo, out, sizeof out, &olen, &op);
  assert(used==(long)mo && op==WS_OP_BIN && olen==4 && memcmp(out,payload,4)==0);

  // server encode (unmasked) round-trips length+payload
  uint8_t enc[32]; size_t el = ws_frame_encode(WS_OP_BIN, payload, 4, enc);
  assert(el==6 && enc[0]==0x82 && enc[1]==4 && memcmp(enc+2,payload,4)==0);

  // partial frame -> 0 (need more)
  assert(ws_frame_decode(masked, 1, out, sizeof out, &olen, &op)==0);
  printf("ws codec: accept-key + masked decode + encode OK\n");
  return 0;
}
