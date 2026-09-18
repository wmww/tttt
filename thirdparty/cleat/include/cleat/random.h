#pragma once

#include <stc/random.h>
#include "types.h"
#include "failable_minimal.h"

failable fill_buffer_with_random_seeds(void* buffer, size_t bytes);
failable random_seed(returns u64);
