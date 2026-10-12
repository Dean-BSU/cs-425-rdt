#ifndef PACKET_H
#define PACKET_H

/**
 * @file packet.h
 * @brief Pure conversion between RDT packet values and wire-format bytes.
 *
 * The wire format is a 10-byte header followed by up to 1024 payload bytes.
 * These functions do not perform socket or file I/O.
 */

#include <stddef.h>
#include <stdint.h>

#define RDT_PACKET_HEADER_SIZE 10U
#define RDT_PACKET_MAX_PAYLOAD 1024U
#define RDT_PACKET_MAX_SIZE \
  (RDT_PACKET_HEADER_SIZE + RDT_PACKET_MAX_PAYLOAD)

typedef enum {
  RDT_PACKET_DATA = 0,
  RDT_PACKET_ACK = 1,
  RDT_PACKET_FIN = 2
} rdt_packet_type_t;

/** Packet values in host byte order. */
typedef struct {
  rdt_packet_type_t type;
  uint32_t sequence;
  uint16_t length;
  uint8_t payload[RDT_PACKET_MAX_PAYLOAD];
} rdt_packet_t;

/*
 * Encode a packet into its wire representation.
 *
 * Multi-byte fields are written explicitly in network byte order; no struct
 * is copied to the wire. Returns 0 on success and -1 for invalid arguments,
 * invalid packet values, or an insufficient output buffer.
 */
int rdt_packet_encode(const rdt_packet_t *packet, uint8_t *wire,
                      size_t wire_capacity, size_t *wire_length);

/*
 * Decode and validate a wire packet, including its reserved field, length,
 * packet type, and Internet checksum. Returns 0 on success and -1 if the
 * datagram is malformed or corrupted.
 */
int rdt_packet_decode(const uint8_t *wire, size_t wire_length,
                      rdt_packet_t *packet);

/*
 * Calculate the RFC 1071 Internet checksum for an arbitrary byte sequence.
 * An odd final byte is treated as the high byte of a 16-bit word.
 */
uint16_t rdt_internet_checksum(const uint8_t *data, size_t length);

#endif
