#include "gbn.h"

static rdt_gbn_action_t action(rdt_gbn_action_type_t type, uint32_t sequence,
                               uint8_t retransmission)
{
  /* Returning one value keeps every state transition explicit to the caller. */
  rdt_gbn_action_t result = {type, sequence, retransmission};
  return result;
}

void rdt_gbn_sender_init(rdt_gbn_sender_t *sender, uint32_t total_packets,
                         uint32_t window, uint32_t timeout_ms)
{
  sender->total_packets = total_packets;
  sender->base = 0U;
  sender->next = 0U;
  sender->window = window;
  sender->timeout_ms = timeout_ms;
  sender->timer_started_ms = 0U;
  sender->consecutive_timeouts = 0U;
  sender->timer_active = 0U;
}

rdt_gbn_action_t rdt_gbn_sender_next(rdt_gbn_sender_t *sender,
                                     uint64_t now_ms)
{
  uint8_t retransmission = 0U;
  uint32_t limit;

  if (sender->base == sender->total_packets) {
    return action(RDT_GBN_COMPLETE, 0U, 0U);
  }

  if (sender->timer_active &&
      now_ms - sender->timer_started_ms >= sender->timeout_ms) {
    /* Go-Back-N restarts at base and resends every outstanding packet. */
    sender->consecutive_timeouts++;
    if (sender->consecutive_timeouts >= RDT_GBN_MAX_CONSECUTIVE_TIMEOUTS) {
      return action(RDT_GBN_GIVE_UP, sender->base, 1U);
    }
    sender->next = sender->base;
    sender->timer_started_ms = now_ms;
    retransmission = 1U;
  }

  limit = sender->base + sender->window;
  /* The overflow check protects the window calculation near UINT32_MAX. */
  if (limit < sender->base || limit > sender->total_packets) {
    limit = sender->total_packets;
  }
  if (sender->next < limit) {
    uint32_t sequence = sender->next++;
    if (!sender->timer_active) {
      sender->timer_started_ms = now_ms;
      sender->timer_active = 1U;
    }
    return action(RDT_GBN_SEND, sequence, retransmission);
  }
  return action(RDT_GBN_WAIT, 0U, 0U);
}

void rdt_gbn_sender_ack(rdt_gbn_sender_t *sender, uint32_t sequence,
                        uint64_t now_ms)
{
  if (sequence < sender->base || sequence >= sender->next) {
    /* Old and impossible ACKs cannot move the send window. */
    return;
  }
  sender->base = sequence + 1U;
  sender->consecutive_timeouts = 0U;
  if (sender->base == sender->next) {
    sender->timer_active = 0U;
  } else {
    sender->timer_started_ms = now_ms;
  }
}

void rdt_gbn_receiver_init(rdt_gbn_receiver_t *receiver)
{
  receiver->next_sequence = 0U;
  receiver->fin_sequence = 0U;
  receiver->complete = 0U;
}

rdt_gbn_action_t rdt_gbn_receiver_data(rdt_gbn_receiver_t *receiver,
                                       uint32_t sequence, uint8_t is_fin,
                                       uint64_t now_ms)
{
  (void)now_ms;
  if (receiver->complete) {
    /* A repeated FIN gets the same final ACK, making completion idempotent. */
    return action(RDT_GBN_COMPLETE, receiver->fin_sequence, 0U);
  }
  if (sequence == receiver->next_sequence) {
    if (is_fin != 0U) {
      receiver->fin_sequence = sequence;
      receiver->complete = 1U;
      return action(RDT_GBN_COMPLETE, sequence, 0U);
    }
    /* A gap or duplicate is answered with the last contiguous sequence. */
    receiver->next_sequence++;
    return action(RDT_GBN_SEND, sequence, 0U);
  }
  return action(RDT_GBN_SEND, receiver->next_sequence == 0U
                                     ? 0U
                                     : receiver->next_sequence - 1U,
                1U);
}
