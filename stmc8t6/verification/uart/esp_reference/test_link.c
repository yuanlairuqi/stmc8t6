#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "robot_bridge.h"
#include "robot_peer.h"
static wire_frame_t command(uint32_t session,uint32_t token,uint8_t motion,uint32_t phone) {
    wire_frame_t f={.type=WIRE_COMMAND,.session=session,.token=token};
    f.payload[0]=motion;f.payload[1]=motion?40:0;f.payload[2]=0xf4;f.payload[3]=1;wire_put32(f.payload+4,phone);return f;
}
static bool armed,apply_ok=true;
static int applied_count,stopped_count;
static uint8_t applied_motion;
static bool fake_ready(void *p){(void)p;return armed;}
static bool fake_apply(void *p,uint8_t motion,uint8_t speed){(void)p;(void)speed;applied_motion=motion;++applied_count;return apply_ok;}
static void fake_stop(void *p){(void)p;++stopped_count;}
static bool fake_stopped(void *p){(void)p;return true;}
static bool fake_active(void *p){(void)p;return armed&&apply_ok;}
int main(void) {
    assert(wire_crc((const uint8_t*)"123456789",9)==0x29b1);
    wire_frame_t f=command(0xdeadbeef,123,1,42),got,reply;
    uint8_t bytes[WIRE_SIZE],bad[WIRE_SIZE];wire_encode(&f,bytes);assert(wire_decode(bytes,&got)&&got.token==123);
    for(int i=0;i<WIRE_SIZE;++i){memcpy(bad,bytes,WIRE_SIZE);bad[i]^=1;assert(!wire_decode(bad,&got));}
    wire_parser_t parser={0};uint32_t rng=21;
    for(int i=0;i<10000;++i){rng=rng*1664525u+1013904223u;wire_feed(&parser,(uint8_t)(rng>>24),&got);assert(parser.used<WIRE_SIZE);}
    unsigned decoded=0;for(int i=0;i<WIRE_SIZE;++i)decoded+=wire_feed(&parser,bytes[i],&got);assert(decoded==1&&got.session==0xdeadbeef);
    // Re-sync after a corrupt frame, split frames, and back-to-back frames.
    memcpy(bad,bytes,WIRE_SIZE);bad[17]^=4;parser.used=0;
    for(int i=0;i<WIRE_SIZE;++i)assert(!wire_feed(&parser,bad[i],&got));
    decoded=0;for(int i=0;i<2*WIRE_SIZE;++i)decoded+=wire_feed(&parser,bytes[i%WIRE_SIZE],&got);assert(decoded==2);

    robot_bridge_t b;robot_peer_t p;bridge_init(&b,true,0xdeadbeef);peer_init(&p,false,NULL);
    assert(bridge_tick(&b,0,&f)&&f.payload[0]==0); // boot handshake
    assert(peer_receive(&p,&f,0,&reply));assert(bridge_receive(&b,&reply,1)&&b.online);
    assert((reply.payload[2]&PEER_DEMO)&&!(reply.payload[2]&PEER_APPLIED));
    bridge_request(&b,1,1,40,1,20);assert(bridge_tick(&b,20,&f));
    assert(peer_receive(&p,&f,20,&reply)&&p.motion==1);assert(bridge_receive(&b,&reply,21)&&b.ack==1);
    bridge_request(&b,2,2,40,2,30);assert(bridge_tick(&b,30,&f));
    assert(peer_receive(&p,&f,30,&reply));wire_frame_t old_reply=reply;
    bridge_request(&b,3,1,40,3,50);assert(b.queued);
    assert(bridge_tick(&b,130,&got)&&got.token==f.token); // same token retry
    assert(peer_receive(&p,&got,130,&reply)&&p.lease_at==30); // never renew duplicate
    assert(bridge_tick(&b,430,&got)&&got.payload[0]==0&&!b.queued&&b.error==BRIDGE_ACK_TIMEOUT);
    assert(!bridge_receive(&b,&old_reply,430));
    assert(peer_receive(&p,&got,430,&reply)&&p.motion==0);assert(bridge_receive(&b,&reply,431));
    assert(!bridge_tick(&b,432,&got)); // no delayed queued movement after a failure
    bridge_request(&b,4,1,40,4,440);assert(bridge_tick(&b,440,&f));
    bridge_request(&b,5,0,0,5,441);assert(bridge_tick(&b,441,&got)&&got.payload[0]==0);
    assert(peer_receive(&p,&got,441,&reply));assert(bridge_receive(&b,&reply,442));
    assert(peer_receive(&p,&f,443,&reply)&&p.motion==0); // reordered old movement cannot restart
    assert(bridge_tick(&b,1042,&got)&&!b.online&&got.type==WIRE_COMMAND&&got.payload[0]==0);
    peer_init(&p,false,NULL);assert(peer_receive(&p,&got,1042,&reply));assert(bridge_receive(&b,&reply,1043)); // reboot recovery
    reply.session++;assert(!bridge_receive(&b,&reply,1044));

    peer_init(&p,false,NULL);f=command(7,1,1,1);assert(!peer_receive(&p,&f,0,&reply));
    f=command(7,1,0,1);assert(peer_receive(&p,&f,0,&reply));f=command(7,2,1,2);assert(peer_receive(&p,&f,100,&reply));
    assert(peer_receive(&p,&f,550,&reply)&&p.lease_at==100);
    got=(wire_frame_t){.type=WIRE_PING,.session=7};assert(peer_receive(&p,&got,590,&reply));
    peer_tick(&p,600);assert(p.motion==0&&p.error==PEER_TIMEOUT);
    f=command(7,3,1,3);assert(peer_receive(&p,&f,0xfffffff0u,&reply));peer_tick(&p,0x1e4);assert(p.motion==0);
    peer_init(&p,false,NULL);f=command(8,0xfffffffe,0,1);assert(peer_receive(&p,&f,0,&reply));
    f=command(8,0xffffffff,1,2);assert(peer_receive(&p,&f,1,&reply));f=command(8,0,2,3);assert(peer_receive(&p,&f,2,&reply)&&p.motion==2);
    f.payload[8]=2;assert(peer_receive(&p,&f,3,&reply)&&p.motion==0&&p.error==PEER_BAD_COMMAND);

    peer_hooks_t hooks={.ready=fake_ready,.apply=fake_apply,.stop=fake_stop,.stopped=fake_stopped,.active=fake_active};
    peer_init(&p,true,&hooks);f=command(9,1,0,1);assert(peer_receive(&p,&f,0,&reply)&&p.error==PEER_OK);
    assert(reply.payload[2]&PEER_APPLIED);
    assert(peer_receive(&p,&f,1,&reply)&&(reply.payload[2]&PEER_APPLIED)); // duplicate STOP keeps measured confirmation
    armed=true;f=command(9,2,1,2);
    assert(peer_receive(&p,&f,10,&reply)&&p.motion==1&&(reply.payload[2]&PEER_APPLIED));
    armed=false;peer_tick(&p,11);assert(p.motion==0&&p.error==PEER_NOT_READY);
    armed=true;apply_ok=false;f=command(9,3,1,3);assert(peer_receive(&p,&f,12,&reply)&&p.motion==0&&p.error==PEER_DRIVER_ERROR);
    assert(applied_count==2&&stopped_count>=3);
    // Unequal durations; fresh heartbeats preserve the phase across a direction reversal.
    peer_init(&p,false,NULL);f=command(10,1,0,1);assert(peer_receive(&p,&f,0,&reply));
    f=command(10,2,1,2);f.payload[8]=1;wire_put32(f.payload+10,300);wire_put32(f.payload+14,200);
    assert(peer_receive(&p,&f,10,&reply)&&p.motion==1&&p.phase_at==10);
    f.token++;assert(peer_receive(&p,&f,200,&reply)&&p.phase_at==10);
    peer_tick(&p,309);assert(p.motion==1);peer_tick(&p,310);assert(p.motion==2&&p.phase_at==310);
    f.token++;assert(peer_receive(&p,&f,400,&reply)&&p.motion==2&&p.phase_at==310);
    peer_tick(&p,510);assert(p.motion==1&&p.phase_at==510);
    assert(reply.payload[10]==1&&reply.payload[11]==2);
    peer_tick(&p,900);assert(p.motion==0&&p.error==PEER_TIMEOUT); // cycle never defeats watchdog
    // A complete cycle command/confirmation survives a phase change while UART is waiting.
    bridge_init(&b,true,11);peer_init(&p,false,NULL);assert(bridge_tick(&b,0,&f));
    assert(peer_receive(&p,&f,0,&reply));assert(bridge_receive(&b,&reply,1));
    bridge_parameters(&b,1,100,200);bridge_request(&b,1,1,40,42,10);assert(bridge_tick(&b,10,&f));
    assert(peer_receive(&p,&f,10,&reply));peer_tick(&p,110);peer_status(&p,&reply);
    assert(bridge_receive(&b,&reply,110)&&b.peer_motion==2&&b.peer_mode==1&&b.ack==42);
    f.token++;wire_put32(f.payload+10,99);assert(peer_receive(&p,&f,111,&reply)&&p.motion==0&&p.error==PEER_BAD_COMMAND);
    armed=true;apply_ok=true;peer_init(&p,true,&hooks);f=command(12,1,0,1);assert(peer_receive(&p,&f,0,&reply));
    f=command(12,2,1,2);f.payload[8]=1;wire_put32(f.payload+10,100);wire_put32(f.payload+14,200);
    assert(peer_receive(&p,&f,10,&reply));peer_tick(&p,110);assert(p.motion==2);
    int stops_before=stopped_count;f.token++;f.payload[1]=60;
    assert(peer_receive(&p,&f,150,&reply)&&p.motion==2&&p.speed==60&&p.phase_at==110&&applied_motion==2);
    assert(stopped_count==stops_before); // same phase speed update never interrupts PWM
    peer_tick(&p,310);assert(p.motion==1);
    puts("PASS: CRC, corrupt/noisy/split stream, UART bridge+STM32 roundtrip, startup STOP, retry/replay leases, ACK loss, STOP preemption, reboot/session protection, wraparound, driver readiness/failure");
    return 0;
}
