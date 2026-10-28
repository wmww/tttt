# Single-threaded checker speed: where the cycles go
The checker's profile is flat (nothing above ~6% since 2026-10-16), so the levers are structural (allocation, frames,
memo layout, code size), each a few percent. Measure by callgrind instructions on a quiet build pair
(`scripts/instrcount.sh --bin`), confirm on wall only on a quiet box or the laptop. History: the "Stage 2" sections
of `git show af936072c:agents/plans/86_multiples.md`; the last round's results are measurements.md
"Single-threaded checker speed".

The data side (bytes per type, cache misses by structure, bun's representation): agents/notes/footprint.md.

## Where the time goes (perf3, 2026-10-24, main 862617c2e, this box quiet: load 2-7, 16 vCPUs / 8 cores)
At the defaults (fast/tttt, warm page cache):
- **vscode 1.40 s**: config 1 ms, load+parse 150, resolve 140 (main thread on a CPU 134-140: nearly serial),
  typecheck 1110 (16 checkers, finished within 3% of each other: the pool doesn't idle). K1: typecheck 9.07 s,
  2.39M types; the 16 checkers make 3.79M types between them (**1.6x duplication**) and spend 17.3 checker-seconds
  against K1's 9.07 (duplication plus SMT: 16 threads on 8 cores). The next big lever, structural (shared graph:
  measurements.md "Shared graph end state").
- **openclaw root 6.2 s** (22.8 GB peak): config 80 ms, load+parse 547 (the loader's own work 589 ms is the critical
  path, the 16 workers busy 7.5 of 9.8 s; TTTT_LOAD_POOL_STATS), resolve 616 (link: aliases **165 ms serial**,
  memory-latency bound at ~550 instructions an alias with prefetch already; parallel walk 176; declare 111),
  typecheck 4.8 s (16 checkers balanced; sys 3.4 of 72 CPU-s, 4.8M faults with huge pages off; `--huge-pages` changed
  nothing measurable with few free 2 MB blocks). The output tail (68.9k diagnostics, 12 MB) was 195 ms in
  error_summary_add()'s quadratic scan and ~35 ms of sorting: fixed (2fd330892, aac3864d6), 41 ms left (sort 13,
  formatting 29).
- effect 0.47 s, drizzle 0.41 s: front end 30-60 ms, the rest checking (8 checkers).

K1 instructions (callgrind, release build): vscode 79.2 G (typecheck 88%, load 8%, of it parse 5.7%, resolve 3.3%);
flat as before: the top self costs are type_at_flow_node 2.0%+0.9%, ty_get_prop 1.9%, infer_from 1.9%,
expr_lookup_names 1.7%, ty_link_get 1.4%, hop_whitespace_slow 1.4%, ty_access_key_type 1.3% (fixed: 862617c2e),
parse_expr 1.2%, atom_table_get 1.2%, intern_slot 1.1%, skip_balanced_braces_at 1.0%. drizzle 5.54 G:
instantiate_inner 4.1%+1.8%, ty_get_prop 2.9%, instantiate_structured 2.6%, intern_slot 2.4%, intern_lookup 1.9%,
mapper_list 1.7% (1.7M calls, already a dedup cache); inference_mapper_impl is 17% inclusive (12% its constraint
checks, relation work tsc does too). cachegrind on fast/tttt drizzle: 4.59 G, stall estimate 1.97 G cycles (D1mr
0.43, DLmr 0.73, I1mr 0.68, DLmw 0.14); intern_grow tops it at 8.8% but as sequential rehash misses cachegrind
can't see prefetched (38.7M instructions: a model artifact), then instantiate_inner 7.7%.

perfsweep (the 2026-10-24 run's cache): K1 check time median 0.20x tsgo's, nothing with tsgo check > 0.2 s above
0.65x. Its wall ratios above 1 (faker 7.5x, nest 11.8x) are page-cache-cold reads at `--threads 1` (faker's load
14.5 s cold, 0.37 s warm), not tttt's work. The worst types ratio, lit/packages/labs/tsserver-plugin 1.40 (3363 vs
2406, 4 ms), is literal types, no runaway: 513 enum literals and their fresh types (tsgo's getDeclaredTypeOfEnum
makes those eagerly too) and ~561 fresh string literals from ty_access_key_type(), one per dot-accessed name
(tsgo makes none for `a.b`): a counting difference.

Tried and dropped (perf3):
- A memo of inference_mapper_impl() on the inference's whole state (candidates, priorities, fixed, flags): 0 hits in
  13k calls on drizzle and 217k on effect; every call sees changed candidates (call.c's non_fixing_mapper() already
  caches the repeats).
- ty_is_related()'s fast path for `source == target` after ty_regular() (75% of check_related()'s calls on tsc
  return there, a 15-instruction prologue each): +0.2..+1.0% instructions (relate.c's inlining limit, below).
- F_SETPIPE_SZ 1 MB on a stdout pipe: no change; the 12 MB write is fast, the time was the summary.
- Candidates left, each under 1%: alias_type_of_symbol() (0.8% of tsc, a dispatcher before value_entry_type()'s memo,
  deliberately unmemoized: the memo below detects cycles); hop_whitespace_slow()'s indentation loop (byte at a time,
  ~0.3% of tsc; word-at-a-time needs padding past the text's NUL); ty_check_member_modifiers()' second lookup (below);
  type_at_flow_node()'s shared-node scan (tsc's own linear sharedFlowNodes).

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

## Relation-count leads against tsgo (cor4, 2026-10-22: react-hook-form after the source-only default constraint)
Method: an instrumented tsgo printing every `recursiveTypeRelatedTo` past the cache (stack depths, recursion ids),
against a pre-order print in tttt's `recursive_related()` (agents/notes/instrumenting_tsgo.md). On bulletproof-react
tsgo makes 84.6k such relations; tttt after cor4's commits 93.4k. Two leads left:
- **skipCaching for small unions**: tsc relates a union source of fewer than four members against a non-union target
  (and a primitive against such a union target) member by member, without `recursiveTypeRelatedTo`: no cache entry,
  no maybe key, no push on the deep-nesting stacks. tttt pushes and caches them (`PathImpl<…> | PathImpl<…> |
  PathImpl<…>` sits on the source stack where tsgo has nothing). Prototyped in is_related_to() (call
  structured_related() instead of recursive_related()): react's assignable cache -12%. Needs a suite run: the extra
  stack entries shift the 100-deep backstop and the cache changes what a failed pair costs later.
- **Indexed-access constraint chains**: inside UseFormResetField's variance measurement tttt relates
  `?[keyof ? & string][…][string] -> undefined` chains (a substitution's base through its constraints) at points where
  tsgo's trace has none; not chased. Align the two traces with a difflib script over normalized lines (markers to `?`,
  ids dropped) to find the first divergence.
