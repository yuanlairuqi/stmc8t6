#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "robot_control.h"
static void cmd(uint8_t *d, uint8_t op, uint8_t speed, uint32_t seq) {
    memset(d,0,16); d[0]=3; d[1]=op; d[2]=speed;
    for(int i=0;i<4;++i)d[4+i]=(uint8_t)(seq>>(8*i));
}
int main(int argc, char **argv) {
    robot_control_t r; uint8_t d[16], out[20];
    robot_init(&r); cmd(d,1,40,1); assert(!robot_command(&r,d,16,0));
    robot_connection(&r,true);
    for(uint8_t op=1;op<=2;++op) {
        cmd(d,op,40,op);assert(robot_command(&r,d,16,100));assert(r.motion==op);
    }
    robot_tick(&r,899);assert(r.motion==2);robot_tick(&r,900);assert(r.motion==0&&r.error==ROBOT_TIMEOUT);
    assert(!robot_command(&r,d,16,901));assert(r.motion==0); // duplicate doesn't restart
    cmd(d,1,60,5);assert(robot_command(&r,d,16,1000));
    assert(!robot_command(&r,d,16,1700));robot_tick(&r,1800);assert(r.motion==0); // replay doesn't extend lease
    cmd(d,1,60,6);assert(robot_command(&r,d,16,2000));
    cmd(d,0,0,2);assert(!robot_command(&r,d,16,2001));assert(r.motion==0); // stale stop still stops
    cmd(d,1,100,7);assert(robot_command(&r,d,16,2100));
    cmd(d,0,0,8);assert(robot_command(&r,d,16,2101));assert(r.motion==0&&r.speed==0&&r.ack==8&&r.error==ROBOT_OK);
    cmd(d,1,40,9);assert(robot_command(&r,d,16,2102));
    robot_connection(&r,false);assert(r.motion==0&&r.speed==0);
    robot_connection(&r,true);
    cmd(d,1,40,0xfffffffe);assert(robot_command(&r,d,16,0xffffff00));
    cmd(d,2,40,0xffffffff);assert(robot_command(&r,d,16,0xffffff01));
    cmd(d,1,40,0);assert(robot_command(&r,d,16,0xffffff02));
    robot_tick(&r,0x222);assert(r.motion==0); // clock + sequence wraparound
    for(size_t n=0;n<16;++n){assert(!robot_command(&r,d,n,20));assert(r.motion==0);}
    cmd(d,5,50,1);assert(!robot_command(&r,d,16,0));
    cmd(d,1,101,1);assert(!robot_command(&r,d,16,0));
    cmd(d,1,0,1);assert(!robot_command(&r,d,16,0));
    cmd(d,0,10,1);assert(!robot_command(&r,d,16,0));
    cmd(d,0,0,1);d[3]=2;assert(!robot_command(&r,d,16,0));
    d[3]=0;d[0]=1;assert(!robot_command(&r,d,16,0));
    // Fuzz variable-length/invalid payloads; valid state stays bounded.
    uint32_t rng=42;
    for(int i=0;i<10000;++i){for(int j=0;j<16;++j){rng=rng*1664525+1013904223;d[j]=(uint8_t)(rng>>24);}
        robot_command(&r,d,(size_t)(i%17),(uint32_t)i);assert(r.motion<=2&&r.speed<=100);}
    robot_connection(&r,true);cmd(d,2,75,0x12345678);assert(robot_command(&r,d,16,12000));
    robot_status(&r,12000,out);assert(out[0]==3&&out[2]==3&&out[3]==255&&out[4]==255);
    assert(out[8]==0x78&&out[11]==0x12&&out[12]==0&&out[16]==12);
    r.battery_valid=true;r.battery_mv=7400;robot_status(&r,12000,out);
    assert(out[2]==7&&out[3]==0xe8&&out[4]==0x1c);
    // Produce a genuine UART-linked v2 status for the Java cross-language test.
    r.uart_configured=true;r.stm_online=true;r.stm_flags=1|4|8;r.stm_motion=2;r.stm_ack=0x12345678;
    robot_status(&r,12000,out);assert(out[2]==(1|4|8|16|32|128));
    if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(out,1,20,f)==20);fclose(f);}
    r.stm_online=false;r.stm_flags=0;cmd(d,1,40,0x12345679);assert(!robot_command(&r,d,16,13000));assert(r.motion==0);
    cmd(d,0,0,0x1234567a);assert(robot_command(&r,d,16,13001)); // stop works even while STM32 offline
    r.uart_configured=false;cmd(d,1,40,0x1234567b);d[3]=1;
    d[8]=0xe8;d[9]=3;d[12]=0xd0;d[13]=7;
    assert(robot_command(&r,d,16,14000)&&r.mode==1&&r.forward_ms==1000&&r.reverse_ms==2000);
    cmd(d,1,40,0x1234567c);d[3]=1;d[8]=99;d[12]=100;
    assert(!robot_command(&r,d,16,14001)&&r.motion==0);
    puts("PASS: motions, release/stop, lease deadline, disconnect, invalid payloads, replay, wraparound, 10000 fuzz packets, wire status");
    return 0;
}
