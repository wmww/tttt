# Escaped identifiers: what's left

Names with unicode escapes (`\u0061`, `a\u{62}`) resolve by their cooked value (2026-09-29): `name_cooked()`
(`span.h`) decodes a view of a registered file's text (files with a `\u`, registered by `parse_file()`, cooked
strings cached per file until it's dropped), and it's applied where names become keys: symbol tables (hash, lookup,
insert), `checker_atom()`, `ty_string_literal()`, and the identifier comparisons that decide meaning (`name_equals()`:
labels, private names, type parameter lookups, overload runs...). The parser decodes escapes into the values it makes
from names (`identifier_name_value()`: dot property names, `name_string` of members, object binding keys).
`Symbol.name` stays raw, which is what tsc prints (`Symbol(a, ...)`, `classType2` in types); messages quote
the source as written.

Left:
- Comparisons of names against literals (`span_equals(name, "constructor")`, `"prototype"`, `"__proto__"`...) are by
  raw text: `constructor` isn't a constructor. tsc compares escapedText there. No test needs it.
- Computed keys compare by source text, JSX attribute names stay raw (TS17021 there anyway).
- unicodeEscapesInNames02(target=es5): errors need tsc's `TS-1` pre/post-emit count mismatch line (not
  reproducible); types print `this.𐊧` for a property access whose name is an invalid character (tsc's node text
  includes it, ours is empty).
