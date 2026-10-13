#include "robot_wire.h"
#include <string.h>
uint16_t wire_crc(const uint8_t *p, size_t n) {
    uint16_t crc=0xffff;
    while(n--) { crc^=(uint16_t)*p++<<8; for(int i=0;i<8;++i) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0)); }
    return crc;
}
uint32_t wire_u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
void wire_put32(uint8_t *p,uint32_t v) { for(int i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
bool wire_newer(uint32_t a,uint32_t b) { uint32_t d=a-b; return d!=0&&d<0x80000000u; }
void wire_encode(const wire_frame_t *f,uint8_t out[WIRE_SIZE]) {
    out[0]=0xa5;out[1]=0x5a;out[2]=2;out[3]=f->type;
    wire_put32(out+4,f->session);wire_put32(out+8,f->token);memcpy(out+12,f->payload,18);
    uint16_t crc=wire_crc(out,30);out[30]=(uint8_t)crc;out[31]=(uint8_t)(crc>>8);
}
bool wire_decode(const uint8_t b[WIRE_SIZE],wire_frame_t *f) {
    if(b[0]!=0xa5||b[1]!=0x5a||b[2]!=2||b[3]<1||b[3]>3||
       wire_crc(b,30)!=((uint16_t)b[30]|(uint16_t)b[31]<<8))return false;
    f->type=b[3];f->session=wire_u32(b+4);f->token=wire_u32(b+8);memcpy(f->payload,b+12,18);return true;
}
bool wire_feed(wire_parser_t *p,uint8_t byte,wire_frame_t *f) {
    p->bytes[p->used++]=byte;
    while(p->used>=2) {
        if(p->bytes[0]==0xa5&&p->bytes[1]==0x5a) {
            if(p->used<WIRE_SIZE)return false;
            if(wire_decode(p->bytes,f)){p->used=0;return true;}
        }
        --p->used;memmove(p->bytes,p->bytes+1,p->used);
    }
    return false;
}
