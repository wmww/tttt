#!/usr/bin/python
import re, sys
x, y, r = 0, 0, re.compile(f'[{sys.argv[2]}]');
for i in range(64):
    x |= (1 if r.match(chr(i)) else 0) << i
    y |= (1 if r.match(chr(i + 64)) else 0) << i
print('''
// Checks if the pointed-at character matches the regex [''' + sys.argv[2] + ''']
// To regenerate run cleat/src/gen_chartest.py ''' + sys.argv[1] + " '" + sys.argv[2].replace("'", "'"'"'"'"'"'"'") + "'"'''
static inline bool ''' + sys.argv[1] + '''(char const* char_ptr) {
    static const u64 magic[] = {''' + hex(x) + ', ' + hex(y) + ''', 0x0, 0x0};
    unsigned char c = *(unsigned char*)char_ptr;
    return (magic[(c >> 6) & 0x3] >> (c & 0x3f)) & 0x1;
}
''')
