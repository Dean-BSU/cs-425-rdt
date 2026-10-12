#include "packet.h"

#include <arpa/inet.h>
#include <string.h>

static int valid_type(uint8_t type)
{
  /* The enum values are deliberately contiguous from DATA through FIN. */
  return type <= (uint8_t)RDT_PACKET_FIN;
}

uint16_t rdt_internet_checksum(const uint8_t *data, size_t length)
{
  uint32_t sum = 0;

  while (length >= 2U) {
    /* The Internet checksum adds 16-bit words in network-byte order. */
    sum += ((uint32_t)data[0] << 8U) | (uint32_t)data[1];
    data += 2;
    length -= 2U;
  }
  if (length != 0U) {
    /* RFC 1071 pads an odd trailing byte on the right. */
    sum += (uint32_t)data[0] << 8U;
  }

  while ((sum >> 16U) != 0U) {
    /* Fold carries back into the low 16 bits until none remain. */
    sum = (sum & 0xffffU) + (sum >> 16U);
  }
  return (uint16_t)~sum;
}

int rdt_packet_encode(const rdt_packet_t *packet, uint8_t *wire,
                      size_t wire_capacity, size_t *wire_length)
{
  uint16_t network_length;
  uint32_t network_sequence;
  size_t total_length;
  uint16_t checksum;

  if (packet == NULL || wire == NULL || wire_length == NULL ||
      !valid_type((uint8_t)packet->type) ||
      packet->length > RDT_PACKET_MAX_PAYLOAD ||
      ((packet->type == RDT_PACKET_ACK || packet->type == RDT_PACKET_FIN) &&
       packet->length != 0U)) {
    return -1;
  }

  /* Reserve checksum bytes while calculating the checksum over the packet. */
  total_length = RDT_PACKET_HEADER_SIZE + (size_t)packet->length;
  if (wire_capacity < total_length) {
    return -1;
  }

  wire[0] = (uint8_t)packet->type;
  wire[1] = 0U;
  wire[2] = 0U;
  wire[3] = 0U;
  network_sequence = htonl(packet->sequence);
  memcpy(wire + 4, &network_sequence, sizeof(network_sequence));
  network_length = htons(packet->length);
  memcpy(wire + 8, &network_length, sizeof(network_length));
  if (packet->length != 0U) {
    memcpy(wire + RDT_PACKET_HEADER_SIZE, packet->payload, packet->length);
  }

  checksum = rdt_internet_checksum(wire, total_length);
  wire[2] = (uint8_t)(checksum >> 8U);
  wire[3] = (uint8_t)(checksum & 0xffU);
  *wire_length = total_length;
  return 0;
}

int rdt_packet_decode(const uint8_t *wire, size_t wire_length,
                      rdt_packet_t *packet)
{
  uint16_t network_length;
  uint32_t network_sequence;
  size_t expected_length;

  if (wire == NULL || packet == NULL || wire_length < RDT_PACKET_HEADER_SIZE ||
      wire_length > RDT_PACKET_MAX_SIZE || !valid_type(wire[0]) ||
      wire[1] != 0U || rdt_internet_checksum(wire, wire_length) != 0U) {
    return -1;
  }

  /* The declared payload length determines the only valid datagram size. */
  memcpy(&network_length, wire + 8, sizeof(network_length));
  packet->length = ntohs(network_length);
  expected_length = RDT_PACKET_HEADER_SIZE + (size_t)packet->length;
  if (packet->length > RDT_PACKET_MAX_PAYLOAD ||
      expected_length != wire_length ||
      ((wire[0] == (uint8_t)RDT_PACKET_ACK ||
        wire[0] == (uint8_t)RDT_PACKET_FIN) &&
       packet->length != 0U)) {
    return -1;
  }

  memcpy(&network_sequence, wire + 4, sizeof(network_sequence));
  packet->type = (rdt_packet_type_t)wire[0];
  packet->sequence = ntohl(network_sequence);
  if (packet->length != 0U) {
    memcpy(packet->payload, wire + RDT_PACKET_HEADER_SIZE, packet->length);
  }
  return 0;
}
