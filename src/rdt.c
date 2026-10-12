#include "rdt.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_unsigned(const char *text, unsigned long maximum,
                          unsigned long *value)
{
  char *end = NULL;
  unsigned long parsed;

  // GCOVR_EXCL_START
  if (text == NULL || *text == '\0' || text[0] == '-') {
    return -1;
  }
  // GCOVR_EXCL_STOP

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || parsed > maximum) {
    return -1;
  }

  *value = parsed;
  return 0;
}

static int parse_probability(const char *text, double *value)
{
  char *end = NULL;
  double parsed;

  if (text == NULL || *text == '\0') {
    return -1;
  }

  errno = 0;
  parsed = strtod(text, &end);
  if (errno == ERANGE || end == text || *end != '\0' || parsed < 0.0 ||
      parsed > 1.0) {
    return -1;
  }

  *value = parsed;
  return 0;
}

void rdt_print_usage(const char *program)
{
  printf("Usage: %s send -s <session> [-w window] [-T timeout-ms] "
         "[-l loss] [-c corrupt] [-d dup] [-p port] <relay> <file>\n"
         "       %s recv -s <session> [-p port] <relay> <file>\n\n"
         "  -s <session>     session shared by sender and receiver\n"
         "  -w <window>      Go-Back-N window, 1 to 64 (default: %u)\n"
         "  -T <timeout-ms>  retransmission timeout (default: %u)\n"
         "  -l <loss>        packet loss probability, 0 to 1 (default: 0)\n"
         "  -c <corrupt>     corruption probability, 0 to 1 (default: 0)\n"
         "  -d <dup>         duplicate probability, 0 to 1 (default: 0)\n"
         "  -p <port>        relay UDP port (default: %u)\n",
         program, program, RDT_DEFAULT_WINDOW, RDT_DEFAULT_TIMEOUT_MS,
         RDT_DEFAULT_RELAY_PORT);
}

int rdt_parse_args(int argc, char *argv[], rdt_config_t *config)
{
  int option;
  unsigned long parsed;
  const char *value;

  if (config == NULL || argc < 2) {
    return -1;
  }

  memset(config, 0, sizeof(*config));
  config->window = RDT_DEFAULT_WINDOW;
  config->timeout_ms = RDT_DEFAULT_TIMEOUT_MS;
  config->port = RDT_DEFAULT_RELAY_PORT;

  if (strcmp(argv[1], "send") == 0) {
    config->mode = RDT_MODE_SEND;
  } else if (strcmp(argv[1], "recv") == 0) {
    config->mode = RDT_MODE_RECEIVE;
  } else {
    fprintf(stderr, "Expected mode 'send' or 'recv', got '%s'.\n", argv[1]);
    return -1;
  }

  optind = 1;
  while ((option = getopt(argc - 1, argv + 1, ":s:w:T:l:c:d:p:")) != -1) {
    switch (option) {
    case 's':
      config->session = optarg;
      break;
    case 'w':
      if (config->mode == RDT_MODE_RECEIVE) {
        fprintf(stderr, "Option -w is only available in sender mode.\n");
        return -1;
      }
      if (parse_unsigned(optarg, 64, &parsed) != 0 || parsed == 0) {
        fprintf(stderr, "Window must be an integer from 1 through 64.\n");
        return -1;
      }
      config->window = (unsigned int)parsed;
      break;
    case 'T':
      if (config->mode == RDT_MODE_RECEIVE) {
        fprintf(stderr, "Option -T is only available in sender mode.\n");
        return -1;
      }
      if (parse_unsigned(optarg, UINT_MAX, &parsed) != 0 || parsed == 0) {
        fprintf(stderr, "Timeout must be a positive integer in milliseconds.\n");
        return -1;
      }
      config->timeout_ms = (unsigned int)parsed;
      break;
    case 'l':
      if (parse_probability(optarg, &config->loss) != 0) {
        fprintf(stderr, "Loss must be a probability from 0 through 1.\n");
        return -1;
      }
      break;
    case 'c':
      if (parse_probability(optarg, &config->corrupt) != 0) {
        fprintf(stderr, "Corruption must be a probability from 0 through 1.\n");
        return -1;
      }
      break;
    case 'd':
      if (parse_probability(optarg, &config->duplicate) != 0) {
        fprintf(stderr, "Duplication must be a probability from 0 through 1.\n");
        return -1;
      }
      break;
    case 'p':
      if (parse_unsigned(optarg, 65535, &parsed) != 0 || parsed == 0) {
        fprintf(stderr, "Port must be an integer from 1 through 65535.\n");
        return -1;
      }
      config->port = (unsigned short)parsed;
      break;
    case ':':
      fprintf(stderr, "Option %s requires a value.\n", argv[optind]);
      return -1;
    case '?':
      fprintf(stderr, "Unknown option: -%c.\n", optopt);
      return -1;
    // GCOVR_EXCL_START
    default:
      return -1;
    // GCOVR_EXCL_STOP
    }
  }

  if (config->session == NULL || config->session[0] == '\0') {
    fprintf(stderr, "A non-empty session is required with -s.\n");
    return -1;
  }
  if (argc - 1 - optind != 2) {
    fprintf(stderr, "Expected <relay> and <file>.\n");
    return -1;
  }

  value = argv[optind + 1];
  if (value[0] == '\0') {
    fprintf(stderr, "Relay address cannot be empty.\n");
    return -1;
  }
  config->relay = value;
  config->file = argv[optind + 2];
  if (config->file[0] == '\0') {
    fprintf(stderr, "File path cannot be empty.\n");
    return -1;
  }

  if (config->mode == RDT_MODE_RECEIVE &&
      (config->loss != 0.0 || config->corrupt != 0.0 ||
       config->duplicate != 0.0)) {
    fprintf(stderr, "Loss, corruption, and duplication are sender options.\n");
    return -1;
  }

  return 0;
}
