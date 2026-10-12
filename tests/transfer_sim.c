#include "transfer_sim.h"

#include "../src/gbn.h"
#include "../src/packet.h"

#include <stddef.h>
#include <string.h>

#define SIM_PAYLOAD_SIZE 3073U
#define SIM_QUEUE_SIZE 4096U
#define SIM_MAX_STEPS 1000000U
#define SIM_FAILURE (-1)

typedef struct {
  uint8_t bytes[RDT_PACKET_MAX_SIZE];
  size_t length;
} datagram_t;

typedef struct {
  datagram_t packets[SIM_QUEUE_SIZE];
  size_t count;
} queue_t;

static uint32_t random_next(uint32_t *state)
{
  uint32_t value = *state;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  *state = value == 0U ? 0x6d2b79f5U : value;
  return *state;
}

static int enqueue(queue_t *queue, const uint8_t *bytes, size_t length,
                   uint32_t *random)
{
  datagram_t *packet;

  if (queue->count == SIM_QUEUE_SIZE || length > RDT_PACKET_MAX_SIZE) {
    return SIM_FAILURE;
  }
  packet = &queue->packets[queue->count++];
  memcpy(packet->bytes, bytes, length);
  packet->length = length;

  if (random_next(random) % 100U < 20U) {
    queue->count--;
    return 0;
  }
  if (random_next(random) % 100U < 20U) {
    packet->bytes[random_next(random) % length] ^=
        (uint8_t)(1U << (random_next(random) % 8U));
  } else if (random_next(random) % 100U < 20U) {
    if (queue->count == SIM_QUEUE_SIZE) {
      return SIM_FAILURE;
    }
    queue->packets[queue->count++] = *packet;
  }
  return 0;
}

static int dequeue(queue_t *queue, datagram_t *packet)
{
  if (queue->count == 0U) {
    return 0;
  }
  *packet = queue->packets[0];
  memmove(queue->packets, queue->packets + 1U,
          (queue->count - 1U) * sizeof(queue->packets[0]));
  queue->count--;
  return 1;
}

int rdt_simulate_transfer(uint32_t seed)
{
  static queue_t to_receiver;
  static queue_t to_sender;
  uint8_t source[SIM_PAYLOAD_SIZE];
  uint8_t received[SIM_PAYLOAD_SIZE];
  rdt_gbn_sender_t sender;
  rdt_gbn_receiver_t receiver;
  uint32_t random = seed == 0U ? 1U : seed;
  uint32_t total_packets =
      (SIM_PAYLOAD_SIZE + RDT_PACKET_MAX_PAYLOAD - 1U) /
          RDT_PACKET_MAX_PAYLOAD +
      1U;
  size_t received_length = 0U;
  uint64_t now = 0U;

  memset(&to_receiver, 0, sizeof(to_receiver));
  memset(&to_sender, 0, sizeof(to_sender));
  for (size_t index = 0U; index < sizeof(source); index++) {
    source[index] = (uint8_t)(index * 29U + 7U);
  }
  rdt_gbn_sender_init(&sender, total_packets, 8U, 25U);
  rdt_gbn_receiver_init(&receiver);

  for (uint32_t step = 0U; step < SIM_MAX_STEPS; step++) {
    rdt_gbn_action_t sender_action = rdt_gbn_sender_next(&sender, now);
    if (sender_action.type == RDT_GBN_GIVE_UP) {
      return SIM_FAILURE;
    }
    if (sender_action.type == RDT_GBN_COMPLETE) {
      return receiver.complete != 0U && received_length == sizeof(source) &&
                     memcmp(source, received, sizeof(source)) == 0
                 ? 0
                 : SIM_FAILURE;
    }
    if (sender_action.type == RDT_GBN_SEND) {
      rdt_packet_t packet = {0};
      uint8_t wire[RDT_PACKET_MAX_SIZE];
      size_t wire_length;
      uint32_t data_packets = total_packets - 1U;

      packet.type = sender_action.sequence == data_packets
                        ? RDT_PACKET_FIN
                        : RDT_PACKET_DATA;
      packet.sequence = sender_action.sequence;
      if (packet.type == RDT_PACKET_DATA) {
        size_t offset = (size_t)packet.sequence * RDT_PACKET_MAX_PAYLOAD;
        packet.length = (uint16_t)((sizeof(source) - offset) >
                                           RDT_PACKET_MAX_PAYLOAD
                                       ? RDT_PACKET_MAX_PAYLOAD
                                       : sizeof(source) - offset);
        memcpy(packet.payload, source + offset, packet.length);
      }
      if (rdt_packet_encode(&packet, wire, sizeof(wire), &wire_length) != 0 ||
          enqueue(&to_receiver, wire, wire_length, &random) != 0) {
        return SIM_FAILURE;
      }
    }

    datagram_t datagram;
    if (dequeue(&to_receiver, &datagram) != 0) {
      rdt_packet_t packet;
      if (rdt_packet_decode(datagram.bytes, datagram.length, &packet) == 0 &&
          packet.type != RDT_PACKET_ACK) {
        uint8_t in_order = packet.sequence == receiver.next_sequence;
        rdt_gbn_action_t receiver_action = rdt_gbn_receiver_data(
            &receiver, packet.sequence, packet.type == RDT_PACKET_FIN, now);
        if (in_order && packet.type == RDT_PACKET_DATA) {
          if (received_length + packet.length > sizeof(received)) {
            return SIM_FAILURE;
          }
          memcpy(received + received_length, packet.payload, packet.length);
          received_length += packet.length;
        }
        if (receiver_action.type == RDT_GBN_COMPLETE ||
            receiver_action.type == RDT_GBN_SEND) {
          rdt_packet_t ack = {
              .type = RDT_PACKET_ACK, .sequence = receiver_action.sequence};
          uint8_t wire[RDT_PACKET_MAX_SIZE];
          size_t wire_length;
          if (rdt_packet_encode(&ack, wire, sizeof(wire), &wire_length) != 0 ||
              enqueue(&to_sender, wire, wire_length, &random) != 0) {
            return SIM_FAILURE;
          }
        }
      }
    }

    if (dequeue(&to_sender, &datagram) != 0) {
      rdt_packet_t ack;
      if (rdt_packet_decode(datagram.bytes, datagram.length, &ack) == 0 &&
          ack.type == RDT_PACKET_ACK) {
        rdt_gbn_sender_ack(&sender, ack.sequence, now);
      }
    }
    if (sender_action.type == RDT_GBN_WAIT || to_receiver.count == 0U) {
      now += 1U;
    }
  }
  return SIM_FAILURE;
}
