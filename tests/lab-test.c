#include <stdio.h>
#include "harness/unity.h"
#include "../src/gbn.h"
#include "../src/packet.h"
#include "../src/rdt.h"
#include "transfer_sim.h"


void setUp(void) {
  printf("Setting up tests...\n");
}

void tearDown(void) {
  printf("Tearing down tests...\n");
}

void test_packet_round_trip(void) {
  rdt_packet_t source = {
      .type = RDT_PACKET_DATA,
      .sequence = 0x12345678U,
      .length = 3U,
      .payload = {'a', 'b', 'c'}};
  rdt_packet_t decoded;
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length = 0U;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&source, wire, sizeof(wire),
                                             &wire_length));
  TEST_ASSERT_EQUAL_UINT(13U, wire_length);
  TEST_ASSERT_EQUAL_HEX8(0U, wire[1]);
  TEST_ASSERT_EQUAL_HEX8(0x12U, wire[4]);
  TEST_ASSERT_EQUAL_HEX8(0x34U, wire[5]);
  TEST_ASSERT_EQUAL_HEX8(0x56U, wire[6]);
  TEST_ASSERT_EQUAL_HEX8(0x78U, wire[7]);
  TEST_ASSERT_EQUAL_HEX8(0U, wire[8]);
  TEST_ASSERT_EQUAL_HEX8(3U, wire[9]);
  TEST_ASSERT_EQUAL_INT(0, rdt_packet_decode(wire, wire_length, &decoded));
  TEST_ASSERT_EQUAL_INT(RDT_PACKET_DATA, decoded.type);
  TEST_ASSERT_EQUAL_UINT32(0x12345678U, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(3U, decoded.length);
  TEST_ASSERT_EQUAL_MEMORY(source.payload, decoded.payload, 3U);
}

void test_packet_rejects_corruption_and_invalid_headers(void) {
  rdt_packet_t packet = {
      .type = RDT_PACKET_DATA, .sequence = 4U, .length = 1U, .payload = {'x'}};
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length = 0U;
  rdt_packet_t decoded;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[10] ^= 1U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));
  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[1] = 1U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));
}

void test_packet_rejects_invalid_arguments_and_capacity(void) {
  rdt_packet_t packet = {.type = RDT_PACKET_DATA, .length = 1U};
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length;

  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(NULL, wire, sizeof(wire),
                                               &wire_length));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(&packet, NULL, sizeof(wire),
                                               &wire_length));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(&packet, wire, sizeof(wire),
                                               NULL));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(&packet, wire,
                                               RDT_PACKET_HEADER_SIZE,
                                               &wire_length));
  packet.type = RDT_PACKET_ACK;
  packet.length = 1U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(&packet, wire, sizeof(wire),
                                               &wire_length));
  packet.type = RDT_PACKET_DATA;
  packet.length = RDT_PACKET_MAX_PAYLOAD + 1U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_encode(&packet, wire, sizeof(wire),
                                               &wire_length));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(NULL, 0U, &packet));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, 0U, NULL));
}

void test_decoder_rejects_ack_with_payload(void) {
  rdt_packet_t packet = {
      .type = RDT_PACKET_DATA, .sequence = 12U, .length = 1U, .payload = {'a'}};
  rdt_packet_t decoded;
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length;
  uint16_t checksum;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[0] = RDT_PACKET_ACK;
  wire[2] = 0U;
  wire[3] = 0U;
  checksum = rdt_internet_checksum(wire, wire_length);
  wire[2] = (uint8_t)(checksum >> 8U);
  wire[3] = (uint8_t)checksum;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));
}

void test_checksum_handles_odd_length_payload(void) {
  rdt_packet_t packet = {
      .type = RDT_PACKET_DATA,
      .sequence = 9U,
      .length = 3U,
      .payload = {'o', 'd', 'd'}};
  rdt_packet_t decoded;
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length = 0U;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  TEST_ASSERT_EQUAL_UINT(13U, wire_length);
  TEST_ASSERT_EQUAL_UINT16(0U, rdt_internet_checksum(wire, wire_length));
  TEST_ASSERT_EQUAL_INT(0, rdt_packet_decode(wire, wire_length, &decoded));
  TEST_ASSERT_EQUAL_MEMORY(packet.payload, decoded.payload, packet.length);
}

void test_checksum_catches_single_flipped_bit(void) {
  rdt_packet_t packet = {
      .type = RDT_PACKET_DATA, .sequence = 10U, .length = 3U,
      .payload = {'b', 'i', 't'}};
  rdt_packet_t decoded;
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length = 0U;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[12] ^= 0x01U;
  TEST_ASSERT_NOT_EQUAL(0, rdt_internet_checksum(wire, wire_length));
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));
}

void test_decoder_rejects_malformed_datagrams(void) {
  rdt_packet_t packet = {
      .type = RDT_PACKET_DATA, .sequence = 11U, .length = 1U, .payload = {'x'}};
  rdt_packet_t decoded;
  uint8_t wire[RDT_PACKET_MAX_SIZE];
  size_t wire_length = 0U;

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  TEST_ASSERT_EQUAL_INT(-1,
                        rdt_packet_decode(wire, RDT_PACKET_HEADER_SIZE - 1U,
                                          &decoded));

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[9] = 2U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));

  TEST_ASSERT_EQUAL_INT(0, rdt_packet_encode(&packet, wire, sizeof(wire),
                                             &wire_length));
  wire[0] = 3U;
  TEST_ASSERT_EQUAL_INT(-1, rdt_packet_decode(wire, wire_length, &decoded));
}

void test_command_line_parser_accepts_and_rejects_inputs(void) {
  rdt_config_t config;
  char *send_args[] = {"myapp", "send", "-s", "demo", "-w", "4", "-T",
                       "500", "-l", "0.1", "-c", "0.2", "-d", "0.3", "-p",
                       "4300", "relay.example", "input.bin"};
  char *recv_args[] = {"myapp", "recv", "-s", "demo", "-p", "4300",
                       "relay.example", "output.bin"};
  char *bad_args[] = {"myapp", "send", "-s", "demo", "relay", "file", "extra"};
  char *empty_session[] = {"myapp", "send", "-s", "", "relay", "file"};
  char *bad_window[] = {"myapp", "send", "-s", "demo", "-w", "0", "relay",
                        "file"};
  char *bad_timeout[] = {"myapp", "send", "-s", "demo", "-T", "0", "relay",
                         "file"};
  char *bad_probability[] = {"myapp", "send", "-s", "demo", "-l", "2", "relay",
                             "file"};
  char *bad_corrupt[] = {"myapp", "send", "-s", "demo", "-c", "x", "relay",
                         "file"};
  char *bad_duplicate[] = {"myapp", "send", "-s", "demo", "-d", "", "relay",
                           "file"};
  char *bad_option_number[] = {"myapp", "send", "-s", "demo", "-w", "x",
                               "relay", "file"};
  char *bad_port[] = {"myapp", "send", "-s", "demo", "-p", "0", "relay",
                      "file"};
  char *receiver_sender_option[] = {"myapp", "recv", "-s", "demo", "-w", "2",
                                    "relay", "file"};
  char *receiver_timeout[] = {"myapp", "recv", "-s", "demo", "-T", "2",
                              "relay", "file"};
  char *receiver_impairment[] = {"myapp", "recv", "-s", "demo", "-l", "0.1",
                                 "relay", "file"};
  char *missing_value[] = {"myapp", "send", "-s"};
  char *unknown_option[] = {"myapp", "send", "-x", "relay", "file"};
  char *bad_mode[] = {"myapp", "other", "relay", "file"};
  char *empty_relay[] = {"myapp", "send", "-s", "demo", "", "file"};
  char *empty_file[] = {"myapp", "send", "-s", "demo", "relay", ""};

  TEST_ASSERT_EQUAL_INT(0, rdt_parse_args(18, send_args, &config));
  TEST_ASSERT_EQUAL_INT(RDT_MODE_SEND, config.mode);
  TEST_ASSERT_EQUAL_UINT(4U, config.window);
  TEST_ASSERT_EQUAL_UINT(500U, config.timeout_ms);
  TEST_ASSERT_EQUAL_UINT(4300U, config.port);
  TEST_ASSERT_EQUAL_INT(0, rdt_parse_args(8, recv_args, &config));
  TEST_ASSERT_EQUAL_INT(RDT_MODE_RECEIVE, config.mode);
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(7, bad_args, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(6, empty_session, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_window, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_timeout, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_probability, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_corrupt, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_duplicate, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_option_number, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, bad_port, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, receiver_sender_option, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, receiver_timeout, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(8, receiver_impairment, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(3, missing_value, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(5, unknown_option, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(4, bad_mode, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(6, empty_relay, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(6, empty_file, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(1, send_args, &config));
  TEST_ASSERT_EQUAL_INT(-1, rdt_parse_args(2, send_args, NULL));
  rdt_print_usage("myapp");
}

void test_gbn_sender_window_ack_and_timeout(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 3U, 2U, 100U);
  next = rdt_gbn_sender_next(&sender, 0U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  next = rdt_gbn_sender_next(&sender, 1U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(1U, next.sequence);
  next = rdt_gbn_sender_next(&sender, 2U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_WAIT, next.type);

  rdt_gbn_sender_ack(&sender, 0U, 10U);
  next = rdt_gbn_sender_next(&sender, 10U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(2U, next.sequence);
  next = rdt_gbn_sender_next(&sender, 110U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(1U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);
  next = rdt_gbn_sender_next(&sender, 111U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(2U, next.sequence);

  rdt_gbn_sender_ack(&sender, 2U, 112U);
  next = rdt_gbn_sender_next(&sender, 112U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
}

void test_gbn_sender_stops_when_window_is_full(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 4U, 3U, 100U);
  for (uint32_t sequence = 0U; sequence < 3U; sequence++) {
    next = rdt_gbn_sender_next(&sender, sequence);
    TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
    TEST_ASSERT_EQUAL_UINT32(sequence, next.sequence);
  }
  next = rdt_gbn_sender_next(&sender, 3U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_WAIT, next.type);
}

void test_gbn_sender_cumulative_ack_slides_multiple_packets(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 5U, 4U, 100U);
  for (uint32_t sequence = 0U; sequence < 4U; sequence++) {
    (void)rdt_gbn_sender_next(&sender, sequence);
  }
  rdt_gbn_sender_ack(&sender, 2U, 10U);
  next = rdt_gbn_sender_next(&sender, 10U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(4U, next.sequence);
}

void test_gbn_sender_ignores_duplicate_ack(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 4U, 3U, 100U);
  for (uint32_t sequence = 0U; sequence < 3U; sequence++) {
    (void)rdt_gbn_sender_next(&sender, sequence);
  }
  rdt_gbn_sender_ack(&sender, 0U, 10U);
  rdt_gbn_sender_ack(&sender, 0U, 11U);
  next = rdt_gbn_sender_next(&sender, 11U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(3U, next.sequence);
}

void test_gbn_sender_timeout_retransmits_whole_window(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 3U, 3U, 100U);
  for (uint32_t sequence = 0U; sequence < 3U; sequence++) {
    (void)rdt_gbn_sender_next(&sender, sequence);
  }
  next = rdt_gbn_sender_next(&sender, 100U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);
  next = rdt_gbn_sender_next(&sender, 101U);
  TEST_ASSERT_EQUAL_UINT32(1U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(0U, next.retransmission);
  next = rdt_gbn_sender_next(&sender, 102U);
  TEST_ASSERT_EQUAL_UINT32(2U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(0U, next.retransmission);
}

void test_gbn_sender_handles_empty_file_as_fin_only(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 1U, 8U, 100U);
  next = rdt_gbn_sender_next(&sender, 0U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  rdt_gbn_sender_ack(&sender, 0U, 1U);
  next = rdt_gbn_sender_next(&sender, 1U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
}

void test_gbn_sender_handles_exact_payload_multiple_before_fin(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 3U, 8U, 100U);
  for (uint32_t sequence = 0U; sequence < 3U; sequence++) {
    next = rdt_gbn_sender_next(&sender, sequence);
    TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
    TEST_ASSERT_EQUAL_UINT32(sequence, next.sequence);
  }
  rdt_gbn_sender_ack(&sender, 2U, 3U);
  next = rdt_gbn_sender_next(&sender, 3U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
}

void test_gbn_sender_gives_up_after_ten_timeouts_without_progress(void) {
  rdt_gbn_sender_t sender;
  rdt_gbn_action_t next;

  rdt_gbn_sender_init(&sender, 1U, 1U, 100U);
  next = rdt_gbn_sender_next(&sender, 0U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);

  for (uint32_t timeout = 1U;
       timeout < RDT_GBN_MAX_CONSECUTIVE_TIMEOUTS; timeout++) {
    next = rdt_gbn_sender_next(
        &sender, (uint64_t)timeout * sender.timeout_ms);
    TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
    TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);
  }
  next = rdt_gbn_sender_next(
      &sender, (uint64_t)RDT_GBN_MAX_CONSECUTIVE_TIMEOUTS *
                   sender.timeout_ms);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_GIVE_UP, next.type);
}

void test_gbn_receiver_acknowledges_in_order_and_duplicates(void) {
  rdt_gbn_receiver_t receiver;
  rdt_gbn_action_t next;

  rdt_gbn_receiver_init(&receiver);
  next = rdt_gbn_receiver_data(&receiver, 0U, 0U, 10U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(0U, next.retransmission);
  next = rdt_gbn_receiver_data(&receiver, 2U, 0U, 11U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);
  next = rdt_gbn_receiver_data(&receiver, 1U, 0U, 12U);
  TEST_ASSERT_EQUAL_UINT32(1U, next.sequence);
  next = rdt_gbn_receiver_data(&receiver, 2U, 1U, 13U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
}

void test_gbn_receiver_handles_duplicate_gap_and_repeated_fin(void) {
  rdt_gbn_receiver_t receiver;
  rdt_gbn_action_t next;

  rdt_gbn_receiver_init(&receiver);
  next = rdt_gbn_receiver_data(&receiver, 0U, 0U, 0U);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  next = rdt_gbn_receiver_data(&receiver, 0U, 0U, 1U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);

  next = rdt_gbn_receiver_data(&receiver, 3U, 0U, 2U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_SEND, next.type);
  TEST_ASSERT_EQUAL_UINT32(0U, next.sequence);
  TEST_ASSERT_EQUAL_UINT8(1U, next.retransmission);

  (void)rdt_gbn_receiver_data(&receiver, 1U, 0U, 3U);
  (void)rdt_gbn_receiver_data(&receiver, 2U, 0U, 4U);
  next = rdt_gbn_receiver_data(&receiver, 3U, 1U, 5U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
  TEST_ASSERT_EQUAL_UINT32(3U, next.sequence);

  next = rdt_gbn_receiver_data(&receiver, 3U, 1U, 6U);
  TEST_ASSERT_EQUAL_INT(RDT_GBN_COMPLETE, next.type);
  TEST_ASSERT_EQUAL_UINT32(3U, next.sequence);
}

void test_complete_transfer_over_impaired_memory_channel(void) {
  TEST_ASSERT_EQUAL_INT(0, rdt_simulate_transfer(1U));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_packet_round_trip);
  RUN_TEST(test_packet_rejects_corruption_and_invalid_headers);
  RUN_TEST(test_packet_rejects_invalid_arguments_and_capacity);
  RUN_TEST(test_decoder_rejects_ack_with_payload);
  RUN_TEST(test_checksum_handles_odd_length_payload);
  RUN_TEST(test_checksum_catches_single_flipped_bit);
  RUN_TEST(test_decoder_rejects_malformed_datagrams);
  RUN_TEST(test_command_line_parser_accepts_and_rejects_inputs);
  RUN_TEST(test_gbn_sender_window_ack_and_timeout);
  RUN_TEST(test_gbn_sender_stops_when_window_is_full);
  RUN_TEST(test_gbn_sender_cumulative_ack_slides_multiple_packets);
  RUN_TEST(test_gbn_sender_ignores_duplicate_ack);
  RUN_TEST(test_gbn_sender_timeout_retransmits_whole_window);
  RUN_TEST(test_gbn_sender_handles_empty_file_as_fin_only);
  RUN_TEST(test_gbn_sender_handles_exact_payload_multiple_before_fin);
  RUN_TEST(test_gbn_sender_gives_up_after_ten_timeouts_without_progress);
  RUN_TEST(test_gbn_receiver_acknowledges_in_order_and_duplicates);
  RUN_TEST(test_gbn_receiver_handles_duplicate_gap_and_repeated_fin);
  RUN_TEST(test_complete_transfer_over_impaired_memory_channel);
  return UNITY_END();
}
