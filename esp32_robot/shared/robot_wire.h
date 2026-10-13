#ifndef ROBOT_WIRE_H
#define ROBOT_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WIRE_SIZE 32
enum { WIRE_COMMAND=1, WIRE_STATUS=2, WIRE_PING=3 };
enum { PEER_READY=1, PEER_DEMO=2, PEER_BATTERY=4, PEER_APPLIED=8, PEER_LOCAL_BUSY=16 };
typedef struct { uint8_t type; uint32_t session, token; uint8_t payload[18]; } wire_frame_t;
typedef struct { uint8_t bytes[WIRE_SIZE]; size_t used; } wire_parser_t;
uint16_t wire_crc(const uint8_t *p, size_t n);
uint32_t wire_u32(const uint8_t *p);
void wire_put32(uint8_t *p, uint32_t value);
void wire_encode(const wire_frame_t *frame, uint8_t out[WIRE_SIZE]);
bool wire_decode(const uint8_t bytes[WIRE_SIZE], wire_frame_t *frame);
bool wire_feed(wire_parser_t *parser, uint8_t byte, wire_frame_t *frame);
bool wire_newer(uint32_t next, uint32_t previous);
#endif
