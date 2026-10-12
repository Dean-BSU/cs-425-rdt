#include "io.h"
#include "gbn.h"
#include "packet.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RDT_MAX_FILE_SIZE (16U * 1024U * 1024U)
#define RDT_REGISTRATION_TIMEOUT_MS 5000U

static uint64_t now_ms(void)
{
  struct timespec time;
  (void)clock_gettime(CLOCK_MONOTONIC, &time);
  /* Monotonic time prevents wall-clock adjustments from breaking timeouts. */
  return (uint64_t)time.tv_sec * 1000U + (uint64_t)time.tv_nsec / 1000000U;
}

static int wait_for_reply(int socket_fd)
{
  struct pollfd descriptor = {socket_fd, POLLIN, 0};
  char reply[128];
  int ready = poll(&descriptor, 1, RDT_REGISTRATION_TIMEOUT_MS);
  ssize_t received;

  if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
    /* Registration has no peer packet to retransmit, so timeout is failure. */
    return -1;
  }
  received = recv(socket_fd, reply, sizeof(reply) - 1U, 0);
  if (received < 0) {
    return -1;
  }
  reply[received] = '\0';
  if (strncmp(reply, "OK", 2U) != 0) {
    fprintf(stderr, "Relay refused registration: %s\n", reply);
    return -1;
  }
  return 0;
}

static int open_relay_socket(const rdt_config_t *config)
{
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  char port[6];
  int socket_fd = -1;
  int result;

  (void)snprintf(port, sizeof(port), "%hu", config->port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  result = getaddrinfo(config->relay, port, &hints, &addresses);
  if (result != 0) {
    fprintf(stderr, "Unable to resolve relay '%s': %s.\n", config->relay,
            gai_strerror(result));
    return -1;
  }
  for (struct addrinfo *address = addresses; address != NULL;
       address = address->ai_next) {
    /* Try each address returned so hostnames work with IPv4 or IPv6. */
    socket_fd = socket(address->ai_family, address->ai_socktype,
                       address->ai_protocol);
    if (socket_fd >= 0 &&
        connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
      break;
    }
    if (socket_fd >= 0) {
      (void)close(socket_fd);
      socket_fd = -1;
    }
  }
  freeaddrinfo(addresses);
  return socket_fd;
}

static int register_with_relay(const rdt_config_t *config, int socket_fd)
{
  char hello[128];
  int length;

  if (config->mode == RDT_MODE_RECEIVE) {
    /* Registration is plain text; packet encoding starts after the OK reply. */
    length = snprintf(hello, sizeof(hello), "HELLO %s recv", config->session);
  } else {
    length = snprintf(hello, sizeof(hello), "HELLO %s send %g %g %g",
                      config->session, config->loss, config->corrupt,
                      config->duplicate);
  }
  if (length < 0 || (size_t)length >= sizeof(hello) ||
      send(socket_fd, hello, (size_t)length, 0) != length) {
    return -1;
  }
  return wait_for_reply(socket_fd);
}

static int sender_run(const rdt_config_t *config, int socket_fd)
{
  FILE *file = fopen(config->file, "rb");
  long file_size;
  uint32_t data_packets;
  uint32_t total_packets;
  rdt_gbn_sender_t sender;
  uint8_t buffer[RDT_PACKET_MAX_SIZE];

  if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
      (file_size = ftell(file)) < 0L ||
      (uint64_t)file_size > RDT_MAX_FILE_SIZE ||
      fseek(file, 0L, SEEK_SET) != 0) {
    fprintf(stderr, "Unable to read input file '%s'.\n", config->file);
    if (file != NULL) {
      (void)fclose(file);
    }
    return -1;
  }
  data_packets = (uint32_t)(((uint64_t)file_size + RDT_PACKET_MAX_PAYLOAD - 1U) /
                            RDT_PACKET_MAX_PAYLOAD);
  /* FIN occupies one sequence number, including for an empty file. */
  total_packets = data_packets + 1U;
  rdt_gbn_sender_init(&sender, total_packets, config->window,
                      config->timeout_ms);

  for (;;) {
    rdt_gbn_action_t action = rdt_gbn_sender_next(&sender, now_ms());
    if (action.type == RDT_GBN_COMPLETE) {
      (void)fclose(file);
      return 0;
    }
    if (action.type == RDT_GBN_GIVE_UP) {
      fprintf(stderr, "Transfer gave up after %u consecutive timeouts.\n",
              RDT_GBN_MAX_CONSECUTIVE_TIMEOUTS);
      (void)fclose(file);
      return -1;
    }
    if (action.type == RDT_GBN_SEND) {
      rdt_packet_t packet = {0};
      size_t wire_length;
      packet.type = action.sequence == data_packets ? RDT_PACKET_FIN
                                                     : RDT_PACKET_DATA;
      packet.sequence = action.sequence;
      if (packet.type == RDT_PACKET_DATA) {
        /* Seek from the sequence number so retransmissions need no cache. */
        if (fseek(file, (long)action.sequence * RDT_PACKET_MAX_PAYLOAD,
                  SEEK_SET) != 0) {
          (void)fclose(file);
          return -1;
        }
        packet.length = (uint16_t)fread(packet.payload, 1U,
                                        RDT_PACKET_MAX_PAYLOAD, file);
        if (ferror(file) != 0) {
          (void)fclose(file);
          return -1;
        }
      }
      if (rdt_packet_encode(&packet, buffer, sizeof(buffer), &wire_length) != 0 ||
          send(socket_fd, buffer, wire_length, 0) != (ssize_t)wire_length) {
        (void)fclose(file);
        return -1;
      }
    } else {
      /* Waiting is the state-machine's request to poll for an ACK. */
      struct pollfd descriptor = {socket_fd, POLLIN, 0};
      if (poll(&descriptor, 1, (int)config->timeout_ms) < 0 &&
          errno != EINTR) {
        (void)fclose(file);
        return -1;
      }
      if ((descriptor.revents & POLLIN) != 0) {
        ssize_t received = recv(socket_fd, buffer, sizeof(buffer), 0);
        rdt_packet_t ack;
        if (received > 0 &&
            rdt_packet_decode(buffer, (size_t)received, &ack) == 0 &&
            ack.type == RDT_PACKET_ACK) {
          rdt_gbn_sender_ack(&sender, ack.sequence, now_ms());
        }
      }
    }
  }
}

static int receiver_run(const rdt_config_t *config, int socket_fd)
{
  FILE *file = fopen(config->file, "wb");
  rdt_gbn_receiver_t receiver;
  uint8_t buffer[RDT_PACKET_MAX_SIZE];

  if (file == NULL) {
    fprintf(stderr, "Unable to open output file '%s'.\n", config->file);
    return -1;
  }
  rdt_gbn_receiver_init(&receiver);
  for (;;) {
    ssize_t received = recv(socket_fd, buffer, sizeof(buffer), 0);
    rdt_packet_t packet;
    if (received <= 0 || rdt_packet_decode(buffer, (size_t)received, &packet) !=
                              0 ||
        packet.type == RDT_PACKET_ACK) {
      /* Invalid datagrams and ACKs are not receiver data; ignore them. */
      continue;
    }
    uint8_t in_order = packet.sequence == receiver.next_sequence;
    rdt_gbn_action_t action = rdt_gbn_receiver_data(
        &receiver, packet.sequence, packet.type == RDT_PACKET_FIN, now_ms());
    if (in_order && packet.type == RDT_PACKET_DATA &&
        fwrite(packet.payload, 1U, packet.length, file) != packet.length) {
      /* Only the next contiguous packet may be delivered to the file. */
      (void)fclose(file);
      return -1;
    }
    rdt_packet_t ack = {.type = RDT_PACKET_ACK, .sequence = action.sequence};
    size_t wire_length;
    if (rdt_packet_encode(&ack, buffer, sizeof(buffer), &wire_length) != 0 ||
        send(socket_fd, buffer, wire_length, 0) != (ssize_t)wire_length) {
      (void)fclose(file);
      return -1;
    }
    if (action.type == RDT_GBN_COMPLETE) {
      (void)fclose(file);
      return 0;
    }
  }
}

int rdt_run(const rdt_config_t *config)
{
  int socket_fd;
  int result;

  if (config == NULL) {
    fprintf(stderr, "Transfer configuration is missing.\n");
    return -1;
  }
  socket_fd = open_relay_socket(config);
  if (socket_fd < 0 || register_with_relay(config, socket_fd) != 0) {
    if (socket_fd >= 0) {
      (void)close(socket_fd);
    }
    return -1;
  }
  result = config->mode == RDT_MODE_SEND ? sender_run(config, socket_fd)
                                         : receiver_run(config, socket_fd);
  (void)close(socket_fd);
  return result;
}
