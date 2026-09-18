#include "random.h"
#include "failable.h"

failable fill_buffer_with_random_seeds(void* buffer, size_t bytes) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return err("failed to open /dev/urandom");
    if (fread(buffer, 1, bytes, f) != bytes) {
        fclose(f);
        return err("failed to read random data");
    }
    fclose(f);
    return ok;
}

failable random_seed(returns u64) {
    u64 ret;
    try (fill_buffer_with_random_seeds(&ret, sizeof(ret))) bubble;
    return ok(ret);
}
