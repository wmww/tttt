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

## Left from the 2026-10-22 round (base ee9501eb8 -> 859a7a6a2: vscode -5.3%, excalidraw -10.9%)
- LiteralBuilder's first realloc; object literal item atoms on the AST; LazyMemo pages.
- The malloc family was 5.6% of vscode's instructions, ~3% after; most of the rest is the loader's path code
  (push_path_to_absolute, relative_path_of, module_resolver_resolve_shared).
- Declined: the distribution's hardening flags (-4 to -5.5% instructions, 0-3% wall: a security-posture change);
  LazySlots as two levels; splitting is_related_to(), recursive_related() or check_related() at their caches.
- Measured not worth it (2026-10-22): flow narrowing (already tsc's algorithm).
- relate.c sits at GCC's inline-unit-growth limit: code added anywhere can cost a hot inlining elsewhere
  (documentation.md "Relations").
