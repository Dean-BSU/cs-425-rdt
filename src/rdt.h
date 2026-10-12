#ifndef RDT_H
#define RDT_H

/**
 * @file rdt.h
 * Reliable Data Transfer.
 */

#include <stdbool.h>
#include <stddef.h>

#define RDT_DEFAULT_RELAY_PORT 4250
#define RDT_DEFAULT_WINDOW 8
#define RDT_DEFAULT_TIMEOUT_MS 250

typedef enum {
  RDT_MODE_SEND,
  RDT_MODE_RECEIVE
} rdt_mode_t;

typedef struct {
  rdt_mode_t mode;
  const char *session;
  const char *relay;
  const char *file;
  unsigned int window;
  unsigned int timeout_ms;
  unsigned short port;
  double loss;
  double corrupt;
  double duplicate;
} rdt_config_t;

/*
 * Parse the command line without constructing or transmitting packets.
 * Returns 0 on success and -1 after writing a diagnostic to stderr.
 */
int rdt_parse_args(int argc, char *argv[], rdt_config_t *config);

void rdt_print_usage(const char *program);

#endif
