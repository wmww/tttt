# Escaped identifiers are named by their raw text

Found at plan 27 stage 3 (2026-09-28). The parser scans unicode escapes in identifiers (`identifier_char_size()`:
`var a = 1`, `class C2 {}`, `a\u{62}`), so they no longer give parse errors, but an identifier's name *is*
its span's text everywhere (`Span.sv`: scope lookups, symbol names, property names, `checker_atom`). So `a` and
`a` are different names, and `var` is an identifier rather than the keyword `var` spelled with an escape.

What tsc does: the scanner cooks the identifier (`tokenValue`/`escapedText`) and keeps the position; names resolve by
the cooked text; `.symbols` print cooked names (`Symbol(a, ...)`), but messages that quote the source
(`Cannot find name '_01234'`, TS2304 uses the node's text) print the raw text. An escaped keyword is the keyword
with TS1260 "Keywords cannot contain escape characters." where it's used as one (`var x`), and an ordinary
identifier where a keyword isn't expected (`var await = 12` is fine outside async code).

Escaped *keywords* are done (2026-09-29): the keyword probe `hop_start_if_ends_on_word_break()` falls back to
`hop_escaped_keyword()` when it mismatches at a `\`, which decodes the word and reports TS1260
(scannerUnicodeEscapeInKeyword1, switchStatementsWithMultipleDefaults pass). What's left is the names.

Tests (~10): escapedIdentifiers, parserUnicode1, parserS7.6_A4.2_T1, scannerS7.6_A4.2_T1,
extendedUnicodeEscapeSequenceIdentifiers, duplicateObjectLiteralProperty, parserClassDeclaration23 (passes, no
cross-reference), scannerUnicodeEscapeInKeyword2 (types: `var \u0061wait` is named `await`),
unicodeEscapesInNames01/02, invalidUnicodeEscapeSequance3.
Real code essentially never escapes identifiers, so this is a fidelity item, not a user-facing one.

Why it isn't a local fix: a `Span` is both the position and the name. Options considered:
- Cook into a side buffer and point the span there: breaks every position computation and the source-order comparisons
  (`span_start_ptr(a) < span_start_ptr(b)`, used-before-declaration) that assume spans point into the file.
- Rewrite the file text in place (`a` -> `a` + padding): the padding ends up in the name, or the span is shorter
  than the source (underlines, parent spans, TS2304's raw text all change).
- Cook at the chokepoints where names become keys: the binder's symbol tables and scope lookups, `checker_atom()`,
  property-name strings (`name_string` is already a separate cooked `cstr` for object literal keys). Names with a `\`
  are rare, so a check for `memchr(sv, '\\')` before cooking into an interned string (per `Program`) costs nothing on
  normal names. This is the plausible route; the work is finding every place a name is compared by its span text
  (`span_equals`, `csview_equals` on `.sv`) that matters for resolution. (TS1260 is done without it, see above.)
