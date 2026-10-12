#include "../tests/transfer_sim.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
  const uint32_t seed_count = 10000U;
  uint32_t failures = 0U;

  for (uint32_t seed = 1U; seed <= seed_count; seed++) {
    if (rdt_simulate_transfer(seed) != 0) {
      failures++;
    }
  }
  printf("transfer simulation passed for %" PRIu32 " of %" PRIu32
         " seeds (%" PRIu32 " gave up)\n",
         seed_count - failures, seed_count, failures);
  return failures == 0U ? 0 : 1;
}
