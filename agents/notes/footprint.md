# The checker's data footprint (2026-10-28, worker foot)

Question (the coordinator, after fe's finding that the idle-tail `.d.ts` files are tsc's work done at a worse
per-instruction speed than bun's): where are tttt's extra bytes and cache misses per type, what does bun's
representation do differently, and which cheap layout changes are output-identical general wins. No perf counters on
this box (perf_event_paranoid 4): cachegrind (`scripts/cgrank.py`, now also `--run path/to/tsconfig.json`) and
`--time`'s arena, `scripts/arenasites.sh`, `/usr/bin/time` peaks.

## Projects
Five shapes at K1 (`--checkers 1 --threads 1`, `--checker-memory off`): two one-file `.d.ts` projects (the idle-tail
files) and three corpora.
- `imm`: `{ "compilerOptions": { "target": "es2015", "module": "commonjs", "noImplicitAny": true, "noImplicitThis":
  true, "strictNullChecks": true, "strictFunctionTypes": true, "lib": ["es2015"], "types": [], "noEmit": true },
  "files": ["<repo>/thirdparty/sources/immutable-js/type-definitions/immutable.d.ts"] }` (the repo's own tsconfig there)
- `store`: `{ "compilerOptions": { "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "strict":
  true, "lib": ["es2022","dom"], "types": [], "noEmit": true }, "files":
  ["<repo>/thirdparty/sources/astro/node_modules/.pnpm/solid-js@1.9.15/node_modules/solid-js/store/types/store.d.ts"] }`
- zod (`tsconfig.test.json`), drizzle, effect: cgrank.py's corpora.

## The census (base 7713f7dfa, release build)
| | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| types made | 448k | 581k | 389k | 440k | 547k |
| checker arena | 227 MB | 144 | 284 | 299 | 298 |
| arena per type | 507 B | 248 | 730 | 680 | 545 |
| peak RSS K1 | 285 MB | 281 | 471 | 520 | 760 |
| bun `--threads=1` peak (1.4.4-canary.20261027.1) | 70 MB | 174 | 211 | 266 | 336 |
| tsgo `--singleThreaded` "Memory used" | 346 MB | 218 | 616 | 589 | 834 |
| wall K1 tttt / bun ST (loaded box) | 0.35 / 0.19 s | 0.56 / 0.42 | 0.69 / 1.11 | 0.84 / 1.31 | 1.48 / 2.41 |

Arena bytes by what they are (arenasites.sh, MB; struct sizes from the debug info):
| what (bytes each) | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| Ty (kind's variant: anonymous 136, reference 104, union 80, param 72, conditional 112) | 50 | 46 | 43 | 46 | 58 |
| instantiated Prop (56; zod 1.22M, drizzle 0.89M of them) | 15 | 5 | 68 | 50 | 31 |
| intern key blocks (value + key words) | 20 | 61 | 27 | 42 | 34 |
| instantiated Signature (128) | 30 | - | 13 | 9 | 11 |
| SigResolved (64) + SigParam arrays (32 a param) | 33 | - | 11 | 7 | 12 |
| cloned type parameters (Ty 72 + TyParamState 88 + mapper) | 15 | - | 2 | - | - |
| Members (72) + props/sorted-name arrays | 22 | 4 | 34 | 28 | 25 |
| mappers (24 + lists) | 13 | 3 | 12 | 8 | 16 |
| mapped props' PropDecl (96) + PropDeclRare (184) | - | - | 19 | 35 | 4 |
| TyObjectState (40, one per object type) | 10 | - | 7 | 5 | 5 |
Outside the arena: the intern tables at 16 bytes a slot, grown at 5/8 load (types 1M slots = 16 MB everywhere here;
store's links table 2M = 32 MB), node links, relation cache.

Where the misses are (cachegrind with the host's caches: D1 48 KB, LL 32 MB; share of read misses):
| | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| LL read misses: intern tables (probe + grow) | 19 + 36% | 35 + 42% | 14 + 21% | 17 + 21% | 9 + 11% (links 13%) |
| D1 read misses: intern tables | 10% | 36% | 10% | 15% | 8% (links 4%) |
| D1 read misses: the type graph (everything else) | 88% | 55% | 81% | 77% | 83% |
- intern_grow's misses are mostly the model's: table_index() is monotonic in the hash, so a doubling rehash reads the
  old table and writes the new one nearly sequentially (prefetched on hardware; fe timed intern_grow at 3-5% of the
  typecheck, mostly faulting the new table). The probes' misses are real: one random slot line, then the key's arena
  block (another line) on a hash match.
- The graph's misses are spread (no line above 5% of D1 misses): instantiate_members() reading every source Prop
  (zod's top line, 4.6%), ty_get_prop()'s binary search dereferencing a Prop per step (fixed below), mapper_list()'s
  cache (82% hits; a hash in the slot would save only the 6% length-matching misses), resolve_call_inner (effect).
- sys time is 10-14% of the K1 typecheck on the one-file projects (imm 50 of 352 ms, 68k faults): every arena byte
  is a fresh page faulted (already populated in batches, arena_populate()); fewer bytes is fewer faults.

## What bun does differently (src/sema at oven-sh/bun 11c3671, read 2026-10-28)
- **Instantiations share the declared type's members**: no per-instance Prop, Signature or Members; a property's type
  is instantiated when read, cached by (TypeId, MapperId). This is most of the 4x on imm (tttt: Prop 56 + Members 72 +
  Signature 104 + SigResolved 64 + params per instance) and of zod's and drizzle's 2x.
- A type is 96 bytes in two arrays (a 64-byte record that is its own intern key, a 32-byte hot summary), u32 ids, no
  pointers; alias and union origin in a boxed side record only the few have.
- Intern index slots are 8 bytes (32-bit hash tag, 32-bit index) at 75% load; lists inline up to 3-7 ids.
- Prop 40 bytes, no declaration record (the binder's 24-byte symbol); Signature ~64, parameters materialized lazily.
- Relation cache 12-byte keys; links dense by node or type id, bit-packed.

## Landed (each output-identical; quickgate --vs-main -k, expecttypes 100%, checkerdiff -k 8 with TTTT_SHARED_GRAPH=1)
| commit | what | arena K1 imm / zod / drizzle / effect | instructions drizzle / zod |
|---|---|---|---|
| a5dc6a1e0 | PropNameIndex: one sorted names array per shape (instances share their source's); lookups read no Prop until found | 227->224 / 284->274 / 299->293 / 298->293 (type-fest 475->454) | -0.22% / -0.37% (type-fest +0.33%: its tuples verify the per-arity hint) |
| 7923be3a4 | mapped props share their PropDecl per (declared prop, mode) (direct-mapped cache, copy on write); PropDeclRare 184->160 | -> 224 / 267 / 273 / 291 | about 0 |
| 796d25fb7 | Signature 128->104 (class_instance, class_type_params, declared_by in SignatureRare) | -> 217 / 265 / 271 / 287 | about 0 |
Together (cachegrind K1, -g release builds): D1 read misses imm -2.4%, zod -1.3%, drizzle -0.5%, effect -0.6%; LL
read misses -0.2..-1.2%; LL write misses zod -3.5%, drizzle -4.7%; instructions -0.2..-0.4%. Peak RSS K1 (fast
builds): imm 277->267 MB, zod 458->439, drizzle 506->476, effect 740->729; at the defaults drizzle ~950->900 MB,
effect ~911->896, vscode 5.42->5.38 GB. Wall K1, interleaved best of 5 at load 7-13: imm 0.34->0.32 s, drizzle
0.80->0.78, others equal within 10 ms.

## What remains is structural (estimates from the census)
1. **Per-instance members** (bun's shared shape): instantiated Props, Members, Signatures, SigResolved and params are
   ~45% of imm's arena, ~40% of zod's, ~30% of drizzle's. Lazily instantiated member types keyed by (prop, mapper)
   would take most of it, and instantiate_members()'s walk of the source props. Not a stint: Prop identity is
   everywhere (prop_type() caches in the Prop, relations and display compare props by pointer, ~850 `decl` reads).
   A step towards it: instance props without `parent` and `decl` (both derivable from the target for
   instantiate_members()'s props, but copies and mapped props break the rule): 56 -> 40 bytes, -7% of zod's arena.
2. **Intern tables**: 8-byte slots (32-bit hash, 32-bit index into a per-table key store) would halve the tables
   (16-48 MB here) and the probe's first line; the key block is still a second line per hit. 35-77% of LL read
   misses are in the tables (less intern_grow's model artifact). The key store must not move (callers keep slot
   pointers) and --shared-graph puts the hash before each block: a careful week.
3. **The Ty header**: alias + alias_args (24 bytes) are set on 3-10% of types (half of conditionals), order_key (8)
   on 2-50%; an anonymous instance uses ~40 of its variant's 88 bytes (node, function, merged_value, overloads,
   free_params are its target's). A shorter allocation for instances needs every read of those fields (~200) to
   check the kind first.
4. Every mapped prop still has a KEY_PROP_MAPPED_TYPE links entry (~50 bytes with its slot, 109k on drizzle) that only
   TS2615's circular path reads; the template prop knows the mapped type, but copies of mapped props keep the
   template as target and would then find it where they find nothing today.

## The laptop's counters (2026-10-27, the human's laptop, main dc8d517a0)
Ryzen AI 9 HX 370 (Zen 5: 48 KB L1D, 1 MB L2 a core, 16 MB L3 for the four big cores), every run `taskset -c 2`
(a big core; the eight Zen 5c cores have half the L3 and 3.3 GHz), box idle. BASE `abbuild.sh 7713f7dfa --fast`, NEW
`fastbuild.sh` at dc8d517a0 (both BOLT), bun 1.4.4-canary.20261027.1 `check --threads=1`; tttt `--checkers 1
--threads 1`, `TTTT_SYNC_EXIT=1`, from an empty directory; immutable.d.ts fetched at immutable-js 686b8baf, store.d.ts
from `npm pack solid-js@1.9.15` (no thirdparty/sources here). `LLC-load-misses` doesn't exist on Zen:
`ls_dmnd_fills_from_sys.local_ccx` (demand fills from L3) and `.dram_io_all` (from DRAM) stand in. Medians of 5
interleaved runs, spread under 3% except the first runs (no multiplexing: six counters):
| | cycles:u M | instr:u M | IPC | L1D miss M | L3 fill M | DRAM fill M | faults | task ms | RSS MB |
|---|---|---|---|---|---|---|---|---|---|
| imm base | 1195 | 2380 | 1.99 | 51.4 | 5.13 | 2.02 | 3051 | 263 | 285 |
| imm new | 1159 | 2373 | 2.05 | 49.5 | 4.65 | 1.86 | 3008 | 257 | 278 |
| imm bun | 815 | 2320 | 2.85 | 18.4 | 2.48 | 0.88 | 15168 | 184 | 67 |
| store base | 2046 | 3050 | 1.49 | 45.5 | 4.13 | 4.11 | 3339 | 431 | 280 |
| store new | 2037 | 3055 | 1.50 | 45.4 | 4.11 | 4.10 | 3278 | 429 | 278 |
| store bun | 1677 | 4821 | 2.87 | 29.2 | 2.52 | 3.09 | 41819 | 384 | 169 |
| zod base | 2348 | 3448 | 1.47 | 69.5 | 7.00 | 4.33 | 4027 | 507 | 466 |
| zod new | 2323 | 3437 | 1.48 | 67.7 | 6.55 | 4.20 | 3954 | 505 | 446 |
| zod bun | 4312 | 8855 | 2.05 | 98.9 | 16.22 | 4.50 | 49065 | 941 | 205 |
| drizzle base | 2950 | 4294 | 1.46 | 80.0 | 8.24 | 5.22 | 3955 | 641 | 515 |
| drizzle new | 2901 | 4292 | 1.48 | 77.8 | 7.78 | 5.02 | 3728 | 628 | 484 |
| drizzle bun | 5303 | 9805 | 1.85 | 111.9 | 18.39 | 6.25 | 73945 | 1174 | 260 |
| effect base | 4884 | 6965 | 1.43 | 139.9 | 14.14 | 8.20 | 4961 | 1043 | 686 |
| effect new | 4828 | 6957 | 1.44 | 138.3 | 13.13 | 7.96 | 4954 | 1034 | 676 |
| effect bun | 9628 | 18746 | 1.95 | 230.4 | 36.16 | 9.85 | 102685 | 2084 | 326 |
- **The three landed changes on hardware**: cycles imm -3.0%, store -0.4%, zod -1.1%, drizzle -1.7%, effect -1.1%;
  L1D misses -3.7 / -0.2 / -2.6 / -2.8 / -1.1%; DRAM fills -8 / 0 / -3 / -4 / -3%; instructions equal. Real but small,
  like cachegrind said.
- Top-down (`perf stat -r 5 -M PipelineL1` and `PipelineL2`, multiplexed; slots times the cycles above, M cycles):
| | cycles | retiring | frontend | backend: memory | backend: cpu | bad speculation |
|---|---|---|---|---|---|---|
| imm new / bun | 1159 / 815 | 255 / 231 | 496 / 346 | 362 / 184 | 12 / 5 | 110 / 51 |
| store new / bun | 2037 / 1677 | 320 / 507 | 650 / 565 | 874 / 475 | 24 / 17 | 208 / 111 |
| zod new / bun | 2323 / 4312 | 372 / 974 | 1175 / 2152 | 602 / 910 | 23 / 34 | 188 / 276 |
| drizzle new / bun | 2901 / 5303 | 458 / 1034 | 1451 / 2657 | 725 / 1145 | 29 / 42 | 241 / 339 |
| effect new / bun | 4828 / 9628 | 739 / 2070 | 2477 / 4853 | 1193 / 2109 | 43 / 67 | 367 / 568 |
  On the corpora both are half front end, a quarter memory (tttt 25%, bun 21%): the same machine profile, tttt
  doing half the instructions. On the one-file `.d.ts` shapes the gap is: imm +344 M = memory +178, front end +150,
  bad speculation +59 at the same instruction count; store +360 M = memory +399, bad speculation +97, front end +85,
  with 37% fewer instructions than bun (all of it lost to memory stalls: store is 43% memory-bound against bun's 28%).
  Bad speculation is twice bun's on both (not in this note's census; branchy code on data-dependent kinds).
- Where the misses are (`perf record -e L1-dcache-load-misses/period=5000/,ls_dmnd_fills_from_sys.local_ccx/period=500/,
  ls_dmnd_fills_from_sys.dram_io_all/period=200/` on a `-g` release build at dc8d517a0, `perf report --sort=srcline`
  bucketed by line: intern = checker.c's hash_words..intern_has, links = ty_link_*, ty_alloc = its first writes to a
  fresh type, kernel = page zeroing and faulting; non-precise, so function-level; IBS `perf mem` gave too few
  samples to use: ldlat's minimum is 128 here and the sample counts were erratic). Shares of each event, %:
| | L1D misses | L3 fills | DRAM fills |
|---|---|---|---|
| imm | graph 74, kernel 10, intern 8, libc 4, ty_alloc 3, grow 1 | graph 74, intern 10, libc 7, kernel 3, ty_alloc 3 | kernel 47, graph 33, ty_alloc 10, intern 6, libc 3 |
| store | graph 47, intern 25, kernel 11, grow 4, ty_alloc 4, relation cache 3, links 3 | graph 55, intern 25, relation cache 5, ty_alloc 5, kernel 5, links 3 | graph 42, intern 25, kernel 13, ty_alloc 9, libc 3, links 2 |
  The intern probes are checker.c:233/239 (the entry's hash, then the key block) and hash_words (skid); the top
  graph line is ty_alloc's first write (checker.c:745, `ret->kind`).

**The verdict, by the rule this section was written for**: the stall share is spread, not the LLC misses' of the
intern tables. imm (the shape where bun is furthest ahead) has 2.7x bun's L1 misses at the same instruction count,
three quarters of them in the type graph and only 8-10% in the tables, and 57% of its DRAM fills are fresh memory
(kernel zeroing plus ty_alloc's first writes): bytes allocated, i.e. **the shared members (structural item 1) first**.
The intern tables (item 2) are the second lever, worth about a quarter of the memory stall on store-like shapes
(~200 M of store's 874 M memory-bound cycles, ~10% of its time; less elsewhere) and 6-10% of imm's. Bad speculation
(twice bun's on the one-file shapes, 5-8% of cycles everywhere) is unexamined.
