#ifndef IO_H
#define IO_H

/**
 * @file io.h
 * @brief Boundary between the protocol state machines and external I/O.
 *
 * This is the only layer that opens sockets, reads clocks, and accesses
 * sender or receiver files.
 */

#include "rdt.h"

/** Run one configured sender or receiver transfer. */
int rdt_run(const rdt_config_t *config);

#endif
