#ifndef GBN_H
#define GBN_H

/**
 * @file gbn.h
 * @brief Pure Go-Back-N sender and receiver state machines.
 *
 * The caller supplies the current time in milliseconds and performs the
 * actions returned by these functions. This layer never uses sockets, files,
 * clocks, or other I/O.
 */

#include <stdint.h>

/** The next operation requested by a Go-Back-N state machine. */
typedef enum {
  RDT_GBN_WAIT,
  RDT_GBN_SEND,
  RDT_GBN_COMPLETE,
  RDT_GBN_GIVE_UP
} rdt_gbn_action_type_t;

/** Number of consecutive timeout events allowed without ACK progress. */
#define RDT_GBN_MAX_CONSECUTIVE_TIMEOUTS 10U

/** A state-machine decision and, when applicable, its packet sequence. */
typedef struct {
  rdt_gbn_action_type_t type;
  uint32_t sequence;
  uint8_t retransmission;
} rdt_gbn_action_t;

/**
 * Sender state.
 *
 * `base` is the oldest unacknowledged packet and `next` is the next packet
 * that can be sent. The active window is the sequence range [base, next).
 */
typedef struct {
  uint32_t total_packets;
  uint32_t base;
  uint32_t next;
  uint32_t window;
  uint32_t timeout_ms;
  uint64_t timer_started_ms;
  uint32_t consecutive_timeouts;
  uint8_t timer_active;
} rdt_gbn_sender_t;

/** Receiver state, including the sequence number expected next. */
typedef struct {
  uint32_t next_sequence;
  uint32_t fin_sequence;
  uint8_t complete;
} rdt_gbn_receiver_t;

/** Initialize a sender before the first call to rdt_gbn_sender_next(). */
void rdt_gbn_sender_init(rdt_gbn_sender_t *sender, uint32_t total_packets,
                         uint32_t window, uint32_t timeout_ms);
rdt_gbn_action_t rdt_gbn_sender_next(rdt_gbn_sender_t *sender,
                                     uint64_t now_ms);
void rdt_gbn_sender_ack(rdt_gbn_sender_t *sender, uint32_t sequence,
                        uint64_t now_ms);

void rdt_gbn_receiver_init(rdt_gbn_receiver_t *receiver);
rdt_gbn_action_t rdt_gbn_receiver_data(rdt_gbn_receiver_t *receiver,
                                       uint32_t sequence, uint8_t is_fin,
                                       uint64_t now_ms);

#endif
/**
 * Decide whether to send a new packet, wait, retransmit, complete, or give up.
 * `now_ms` must be monotonic relative to earlier calls.
 */
/** Process a cumulative ACK received at `now_ms`. */
/** Initialize a receiver to expect sequence number zero. */
/**
 * Process one decoded DATA or FIN sequence number.
 * `is_fin` is nonzero for a FIN packet; the returned action tells the I/O
 * layer which ACK or completion response to produce.
 */
