# Single-threaded checker speed: where the cycles go
The checker's profile is flat (nothing above ~6% since 2026-10-16), so the levers are structural (allocation, frames,
memo layout, code size), each a few percent. Measure by callgrind instructions on a quiet build pair
(`scripts/instrcount.sh --bin`), confirm on wall only on a quiet box or the laptop. History: the "Stage 2" sections
of `git show af936072c:agents/plans/86_multiples.md`; the last round's results are measurements.md
"Single-threaded checker speed".

## The stall ranking (cachegrind on pgo/tttt at K1, 2026-10-19; `scripts/cgrank.py`)
drizzle 5.56 G instructions, I1 miss rate 1.63%, D1 2.7%, 2.26 LL misses per 1000 instructions; tsc 3.92 G, 1.55%,
2.4%, 1.55. About 1.2 instructions per cycle on drizzle; the stall estimate (D1mr*12 + DLmr*150 + I1mr*10 + DLmw*20)
is about half the cycles. Shares of it:
1. **Instruction footprint, ~42% on both**: spread out (instantiate_inner has 7% of drizzle's I1 misses), capacity
   misses (`-fipa-reorder-for-locality` changed nothing). Only smaller hot paths help: outlining cold arms of the
   giant switches (instantiate_inner, is_related_to's, structured_related_members), fewer duplicated inlined bodies.
   Unverified without counters (the uop cache hides part).
2. **Hash tables, ~10% drizzle / 5% tsc**: Checker.types and .links (drizzle 443k and 256k entries, 16 + 8 MB),
   node_links (ty_link_get is tsc's top line); the first probe is a random line. Candidates: 8-byte entries (a hash
   tag plus a 48-bit pointer), the hottest kinds (KEY_ANONYMOUS_INSTANCE, KEY_CONDITIONAL_INSTANCE: 40% of drizzle's
   lookups) in their own table, or a memo on the Ty for the commonest kind.
3. **Ty header reads in hot predicates, ~5%** (`ty->flags` in ty_instantiate()'s inline test, union member walks).
   Big-union walks memoized where they showed (KEY_FLOW_NOT_UNIT: tsc -2.3%).
4. First writes to fresh memory, 8% of the estimate but mostly a model artifact; the real cost is page faults
   (drizzle 0.1 s sys of 1.3 s): allocation volume.
5. Cold source text, 1-3% (memchr in source_file_make(): fuse into the scanner).

## Left from the 2026-10-22 rounds (k1: ee9501eb8 -> 859a7a6a2, vscode -5.3%, excalidraw -10.9%; ck: 08a80d884 -> 23c882120, drizzle -1.8%, type-fest -1.5%, zod -1.3%; vscode -0.43% for the commits after 1f2699984)
- The checker's instruction profile is near its floor for local fixes: what is left is tsc's algorithms (variance
  measurement is ~29% of drizzle and tsgo measures more of them, 737 against 719; flow narrowing; the relation and
  intern table probes at their load factors) and per-element work proportional to the inputs (type-fest's tuples of
  hundreds of elements are hashed, compared and scanned per reference). Per-site wins left are 0.1-0.5%.
- Seen and left: ty_check_member_modifiers() looks the accessed property up again after the access resolved it
  (member_refs(): ~0.5% of vscode; keeping the access's props per node is 1.7M accesses on vscode, ~27 MB at 16 bytes
  each: skipped, memory). The dropped reports behind drizzle's display cost are errors a `@ts-expect-error` suppresses
  (drizzle 240 of 273, vue 382 of 390, effect 203 of 208, webpack 19 of 442, vscode 35 of 45: type tests): explained
  and printed in full, then dropped by program_apply_comment_directives(). Their display is 2.6% of drizzle (huge
  types), under 0.5% of vue, effect and webpack; the explaining runs themselves are relation work tsc does too.
  Skipping the display of a suppressed error isn't free of observable effects: printing resolves member types, which
  tttt relies on for circularity diagnostics (lower.c's indexed access TS2339 path, expr.c's satisfies TS7023); the malloc family is
  ~2% of drizzle and excalidraw, spread over sites of 0.1-0.3% each (inference's candidate arrays, builder_finish()'s
  copies), most of the rest the loader's and the parser's strings (cstr_append_n from module resolution and
  parse_binding_type_ex). Object literal item atoms on the AST: checker_atom() for literal items is ~0.25% of
  excalidraw, not worth an AST field the parser and checkers share. LazyMemo pages: lazy_type_state_peek() is ~1% of
  tsc, a TLS read and two dependent loads; LazySlots as two levels was declined before.
- type-fest's PGO build is bistable by ~0.9% in ty_reference_with_this() (0x144e or 0x14c1 bytes, 75M instructions
  apart): a change elsewhere can flip it. Compare type-fest against a base, not the commit before, and look at
  `nm -S` before attributing a type-fest delta.
- Declined (2026-10-22, ck): a direct-mapped (members, name) cache in front of ty_get_prop()'s binary search (55% hit
  rate on drizzle, PGO +0.34% drizzle, +0.47% excalidraw; the binary search's misses are the stall, unmeasured);
  outlining instantiate_conditional()'s restricted mapper (type-fest +1.0% by an LTO inlining shift elsewhere);
  outlining cold arms in general: cachegrind's I1 misses on drizzle are spread (the top function,
  instantiate_anonymous, has 4.6% of them) and PGO already splits .cold parts.
- Declined: the distribution's hardening flags (-4 to -5.5% instructions, 0-3% wall: a security-posture change);
  LazySlots as two levels; splitting is_related_to(), recursive_related() or check_related() at their caches.
- Measured not worth it (2026-10-22): flow narrowing (already tsc's algorithm).
- relate.c sits at GCC's inline-unit-growth limit: code added anywhere can cost a hot inlining elsewhere
  (documentation.md "Relations"). A struct grown in Checker or a changed inline in members.c can also set off GCC's
  bogus -Wstringop-overflow under LTO (sw_load() inlined into props_related_impl(): "destination object is likely at
  address zero"), which -Werror makes fatal in pgobuild.sh only.
