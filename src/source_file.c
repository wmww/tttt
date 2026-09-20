#include "source_file.h"

SourceFile source_file_make(cstr canonical_path, cstr contents) {
    check(cstr_size(&canonical_path));
    const char* start = cstr_str(&contents);
    const char* end = start + cstr_size(&contents);
    u32 cap = 1024;
    u32* line_starts = malloc(sizeof(u32) * cap);
    u32 line_count = 0;
    line_starts[line_count++] = 0;
    for (const char* pos = start; (pos = memchr(pos, '\n', (size_t)(end - pos))); ) {
        pos++;
        if (line_count == cap) {
            cap *= 2;
            line_starts = realloc(line_starts, sizeof(u32) * cap);
        }
        line_starts[line_count++] = (u32)(pos - start);
    }
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
        .line_starts = line_starts,
        .line_count = line_count,
    };
}

unsigned source_file_line_index(SourceFile const* me, const char* pos) {
    u32 offset = (u32)(pos - source_file_start(me));
    // The last line that starts at or before offset
    u32 low = 0;
    u32 high = my line_count;
    while (high - low > 1) {
        u32 mid = low + (high - low) / 2;
        if (my line_starts[mid] <= offset) low = mid;
        else high = mid;
    }
    return low;
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    free(my line_starts);
    block_drop(&my ast);
    cstr_drop(&my decl_override);
}

