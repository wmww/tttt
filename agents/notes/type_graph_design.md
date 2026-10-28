# The leaner type graph: members shared across instantiations (worker graph, 2026-10-28, design only)

The question (the human, 2026-10-29): a leaner type graph is "strictly better whatever the threading", if it runs as
fast or faster single-threaded; its downsides; can it land incrementally. The laptop's counters (footprint.md "The
laptop's counters") ordered the work: on immutable.d.ts tttt has 2.7x bun's L1D misses at the same instruction count,
three quarters in the type graph, 57% of DRAM fills are fresh memory: **bytes allocated per instantiation first**
(this note), the intern tables second. Nothing here is built. Tools: `scripts/fieldcensus.py` (struct field uses
from clang's AST, exact), `scripts/graphcensus.diff` (the counters below, an out-of-tree build).

## 1. What an instantiation allocates today
`instantiate_members()` (members.c), called for every instantiated anonymous type and every reference's members
(`resolve_instantiated_members()`, `resolve_members_with_this()`), allocates per instance:
| object | bytes | per-instance state | copies of declared data |
|---|---|---|---|
| Members (props array, counts, call/construct arrays, index, mapped, rare) | 72 + 8/prop + 8/signature | none | all of it (the props array is the source's order, by_name already shared: a5dc6a1e0) |
| Prop, one per source prop | 56 | `type` (8, the instantiated type, a SharedWord), `type_flight` (1), `mapper` (8: the instance's) | name, flags, parent, thisless/mapped/is_template bits, `target` (the source prop), `decl` (= ty_prop_decl_none) |
| Signature, one per source signature | 104 | `res`, `ret`, `predicate` words + 3 flight bytes (27), `mapper` | flags, spans, ctx, `target` |
| IndexInfo, eagerly instantiated (`ty_instantiate()` of key and value) | 64 | the instantiated key/value | the rest |
| MappedInfo (mapped types), `ty_mapped_info_instantiate()` | 88 | instantiated constraint, name type, modifiers | the rest |
| On resolution: SigResolved (64) + SigParam (32 a parameter) | | instantiated parameter types, type params (clones) | names, flags, `decl`, counts, target/mapper (16, copies of Signature's) |
| The Ty (anonymous 136 / reference 104) + TyObjectState (40) | | target + mapper / args, the members word | |

Counted at K1 (census build of 3100c2f50 = `scripts/graphcensus.diff`, release, `--checkers 1 --threads 1`):
| | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| checker arena (`--time`) | 218 MB | 143 | 265 | 271 | 287 |
| Props allocated | 328k | 93k | 1.39M | 1.09M | 713k |
| of them instance Props (instantiate_members) | 290k | 92k | 1.30M | 964k | 582k |
| ... whose type is ever computed | 184k (63%) | 91k (99%) | 241k (18%) | 408k (42%) | 237k (41%) |
| ... touched at all (typed, or read out of a props array: a lower bound) | >= 212k (73%) | >= 92k (100%) | >= 296k (23%) | >= 614k (64%) | >= 369k (63%) |
| ... whose instantiated type is the target's own (tsc would have shared the symbol) | 22k | 90k | 73k | 104k | 85k |
| this-only instances sharing a thisless prop (no Prop made) | 0.4k | 50k | 2k | 38k | 5k |
| instance Members (instantiate_members calls) | 227k | 1.6k | 157k | 153k | 160k |
| instance Signatures | 248k | 1.4k | 107k | 75k | 88k |
| instance Props x 64 B (Prop + array slot) | 18.6 MB (8.5%) | 5.9 (4%) | 83 (31%) | 62 (23%) | 37 (13%) |
| instance Signatures x 112 B | 27.7 MB (13%) | 0.2 | 12.0 (4.5%) | 8.4 (3.1%) | 9.9 (3.4%) |

Bytes that are **per-instance state**: one type word per prop (8, plus a flight byte), three words per signature,
the SigResolved contents, the eager index infos and mapped info. Everything else in a Prop (48 of 56 B) and a
Signature (~77 of 104 B) copies or points back to the source. tsc does the same (instantiateSymbol: a transient
symbol per member sharing the target's data, ValueSymbolLinks{resolvedType, target, mapper}) except that it returns
the declared symbol itself when its type is already resolved and holds no type variable (a history-dependent identity
tttt deliberately doesn't copy). bun (src/sema check/shape.rs, read at 11c3671) has no per-instance member at all: a
Prop is a 40-byte value in a shared shape, and `type_of_prop(prop, outer_mapper)` instantiates on read, cached by
(TypeId, MapperId) in its instantiation table; per-instance symbol links exist only on a cold path.

## 2. The census of Prop identity and per-instance fields (clang AST, `scripts/fieldcensus.py Prop ...`)
| use | sites | functions | what it needs |
|---|---|---|---|
| `->decl` reads (PropDecl: spans, symbol, function, node, rare...) | 730 | 146 | the declared prop's, but **an instance's is ty_prop_decl_none** today: readers either walk `target` first or rely on the zeros |
| `->flags` / `->name` reads | 245 / 235 | 113 / 117 | the declared prop's (copied) |
| `->target` reads | 214 | 81 | ~86 are walks to the root (`while (p->target) p = p->target`, prop_root(), ty_prop_decl_span()...: the declared prop); the rest test "is an instance" (prop_type_slow's reentry and RESOLVING_PROP rules, display_structure's annotation reuse, spread/copy rules) |
| `->parent` reads | 55 | 35 | the declared prop's (copied) |
| `->mapper` reads | 9 | 6 | the instance's |
| `->type` / `type_flight` (SharedWord protocol, address taken) | 13 / 7 | 6 / 2 | the instance's state |
| bits: mapped 17, thisless 13, synthetic 7, is_template 5 | 42 | 30 | the declared prop's (mapped/synthetic props are never instances) |
| `Prop *` == / != (both operands props) | 31 | 24 | identity of the pair (relate.c's `source_prop.prop == target_prop` shortcut, private/protected root comparisons, ty_props_distinct, display's `prop != declared`, merged-value dedup) |
| `Prop *` as an integer key | 10 | 9 | identity: KEY_PROP_WRITE_TYPE, KEY_PROP_REENTERED_TYPE, KEY_PROP_MAPPED_TYPE, KEY_SYNTHETIC_PROP_PARTS, fingerprint, mapped_prop_decl's cache, object literal alternatives |
| `void *` identities (not casts): RESOLVING_PROP stack entries, TAINT_*(TK_PROP_TYPE), hub_trace | 6 + 13 + 2 | | identity of the instance |
| functions taking or returning `Prop *` | 84 + 34 | | the handle type |
| struct fields holding a `Prop *` (FoundProp.prop: 49 reads, SyntheticParts, ObjectLiteralProp.reuse, PropDeclRare.mapped_from, mapped decl cache, iterators) | ~15 | | the handle type |
| Members field reads: props 151 + `->props[` 178 expressions, prop_count 184, call 77 / construct 56 / counts 250, index 197, mapped 101 | 1027 | ~120 | the instance's member list |
| Signature: `function` 119, flags 39, target 32 reads; `Signature *` ==/!= 32 sites (20 functions), as key 5 | 300 | 47 | as Prop; SigResolved (601 reads) is per-instance content |
| baseline_test_traversal.c (symbols baseline): 70 decl, 30 target reads | | | the declared prop (walks) |

Grouped by need: **the declared prop** (decl/flags/name/parent/bits, root walks: ~1300 reads), **the instance's
type** (prop_type(): ~150 call sites through one function), **the instance's identity** (31 compares, 10 keys,
~21 void* identities: ~60 sites), **the owning Members' list** (~330 props-array reads). The cost estimate: the
handle type touches ~120 functions and ~15 struct fields; the field reads are mechanical behind accessors.

## 3. The target shape
**Shapes are shared, instances are views.** For instantiate_members() without dynamic bases (`base_count == 0`:
nearly all), the instance's Members becomes a *view*: a Members whose `props`, `call`, `construct` arrays and name
index are the source's (no copy), plus `mapper`, a `source` pointer and one slot array: a type word per prop (and
later three words per signature), allocated with the view (fresh zero memory, written only when a type is
computed). Index infos and the mapped info stay eagerly instantiated (they resolve things; see §5). Instances with
dynamic bases, inherited members of raw class/interface members, mapped, reverse mapped, spread, intersection
(synthetic) and object literal props stay materialized Props as today: they are made once per declaring shape or are
fresh members, not per instance.

**The handle**: `typedef struct PropRef { Prop const* prop; uintptr_t at; } PropRef;` (16 bytes, two registers).
`at == 0`: `prop` is a materialized Prop and is the property, as today. `at != 0`: the view's address with the
prop's index in the top 16 bits (user addresses are below 2^47; shapes over 65535 props stay materialized), and
`prop` is the source's Prop. Identity is preserved exactly: today two instance Props are the same object iff they
are the same index of the same instance Members, which is (view, index) equality. Accessors reproduce today's
fields so the conversion is output-identical by construction:
- `pr_name/flags/parent/bits(r)` = `r.prop->...` (copies today, one load either way);
- `pr_decl(r)` = `at ? &ty_prop_decl_none : r.prop->decl` (an instance's decl is the zeros today);
- `pr_target(r)` = `at ? (PropRef){r.prop, 0} : (PropRef){r.prop->target, 0}`; `pr_mapper(r)` the view's;
- `pr_type(me, r)`: `at == 0` -> prop_type(); else the slot word: done -> the type; empty -> instantiated_prop_type()
  with (source prop, view mapper) and the same in-flight/reentry rules keyed by the slot (KEY_PROP_REENTERED_TYPE by
  (view, index)); the flight byte becomes an in-flight sentinel in the slot word (the default path is single-owner;
  `--shared-graph`'s ClaimSet keys by the word's address already);
- `pr_eq(a, b)`, `pr_key(r)` (two key words where one was);
- `members_prop(m, i)` -> PropRef: `{ m->props[i], is_view(m) && !(this_only && thisless) ? m | i << 48 : 0 }` (a
  this-only view's thisless props are the source's own today: same identity).
`Members.props` is renamed so every reader fails to compile until it goes through `members_prop()`.

Rejected handles: (a) (type, index) with the prop derived: 4-5 dependent loads per field read (type -> state ->
members -> props -> prop) at ~1300 field reads; (b) bun's (prop, outer mapper) with the type in a global
(type, mapper) cache: a hash probe per type read where today's is one load, and the intern probes are already 8-25%
of the misses; (c) a thin per-prop instance object (tsc's transient symbol, ~24 B): keeps Prop* identity and the
smallest conversion, but keeps per-prop bytes and first writes (zod 31 MB): the fallback if §4's step 2 is judged too
costly.

**Name lookup**: ty_get_prop() already reads only the shared names array (PropNameIndex) until found; it returns
`members_prop(m, index)`. The 8-prop filter word is copied into the view at creation. **Signatures** follow the same
pattern one step later: `SigRef` = (source Signature, view|index|kind), slots of three words (res, ret, predicate) per
signature; SigResolved drops its target/mapper copies (64 -> 48); parameters stay materialized (their instantiated
types are the per-instance content; bun's lazy parameters are a later option). **Mapped / reverse mapped / spread /
intersection members** are created, not instantiated: they keep Props (`at == 0`); their PropDecl sharing (7923be3a4)
stays. **The parked shared graph**: published words are the same kinds (a slot word is a Prop.type word at a
different address); views are published with the members word like Members today; nothing new is an identity
created after publication except §4 step 1's lazily materialized Props, which must be CAS'd into their array slot
before the pointer is returned (the loser adopts the winner's: no escape).

**Kind dispatch** (the laptop's bad speculation, 2x bun's on the one-file shapes, unexamined): views route instance
props to one slot path instead of prop_type_slow()'s chain of source tests, and root walks get at most one step for
view props (the 86 walks have data-dependent trip counts today). `pr_type()` adds one branch (`at`), biased by shape.

## 4. The staging (each step output-identical, its own gate; estimates from §1's census)
Gate per step: `quickgate.sh --vs-main -k` (suite incl. symbols/types components, tsc/zod/drizzle 0/0, instructions),
`expecttypes.py` 100%, `checkerdiff.py -k 8` with and without `TTTT_SHARED_GRAPH=1`, `kdiff.py -k 16` on the
five census projects and dokploy/monkeytype (hub_regime.md), `buncases.py` (K-dependence column unchanged); bytes:
`--time` arena and `/usr/bin/time` RSS at K1 and the defaults; cachegrind D1/LL read and write misses
(`cgrank.py --run`); then the laptop: `perf stat` cycles, L1D misses, DRAM fills (`ls_dmnd_fills_from_sys.dram_io_all`)
and page faults on imm, store, zod, drizzle, effect (footprint.md's protocol).

| step | what | days | arena saved: imm / store / zod / drizzle / effect | risk |
|---|---|---|---|---|
| 1 | **Lazily materialized instance Props**: instantiate_members() reserves the props array and writes nothing into it; `members_prop(m, i)` materializes the slot's Prop on first access (identical contents; CAS with the graph). `Members.props` renamed: ~330 reads converted, compile-enforced | 2-3 | <= 4.4 MB (2%) / 0 / 56 MB (21%) / 20 MB (7%) / 12 MB (4%) | low: no computation moves; allocation order of Props changes (addresses as intern keys: not observable, sg2's audit) |
| 2 | **PropRef everywhere**, with `at == 0` always: the handle type through the ~120 functions and ~15 fields, accessors for every field read, FoundProp.prop, the keys. Pure refactor | 6-9 | 0 (+8 B per stored handle: small counts) | instructions (16-byte handle): budget +0.5% on drizzle/zod |
| 3 | **Views for instance props**: shared props array + slot array, `at != 0` | 3-5 | (with 1) 16 MB (7%) / 5 MB (4%) / 73 MB (28%) / 54 MB (20%) / 33 MB (11%) | the decl/target semantics (accessors make them identical); this-only identity; prop_type_slow's reentry rules |
| 4 | **Signature views** (SigRef, 3 slot words per signature; SigResolved 64 -> 48) | 6-10 | +20+5 MB (11%) / 0 / +9+2 (4%) / +6+1 (3%) / +7+2 (3%) | Signature identity (32 compares, 5 keys); resolution re-entry (sig_resolving frames by slot) |
| 5 | Shorter Members header for views, drop root walks for view props, PropDecl reads off the declared prop directly | 3-5 | small | none new |

Independent of these and of each other: the intern tables' 8-byte slots (footprint.md item 2: ~10% of store's time,
6-10% of imm's misses), the shorter Ty header for anonymous instances (item 3).

**Step 1 is the first stint**: it cuts fresh bytes and first writes per instantiation (instantiate_members() no longer
writes 56 B per source prop; its loop disappears), its conversion is mechanical and compile-enforced, and
`members_prop()` is the seam steps 2-3 change behind. Its census is a lower bound of what step 3 takes on zod
(touched 23%), little on imm (touched 73%: imm needs steps 3-4).

**Step 1 as landed** (ac1f53116, fc64a1ea3, c28559e09; K1, release, this box): instance Props made on first use /
instance Props: imm 208k / 290k (72%), store 92k / 92k, zod 266k / 1.30M (20%), drizzle 469k / 964k (49%), effect
307k / 582k (53%) (callgrind calls of members_prop_make()). Arena imm -1.8%, zod -20.4%, drizzle -9.2%, effect -4.5%,
store 0; peak RSS zod -12.2%, drizzle -5.0%; minor faults zod -12.5%, drizzle -5.1%; cachegrind LL write misses zod
-15%, drizzle -6%; instructions after c28559e09 drizzle -0.2%, zod -0.9%, effect ~+0.1% against 6d5e31b4c. What still
makes them: ty_get_prop() callers that read the type (most), props_related_impl, resolve_tuple_members (a tuple's
members are its array instance's props: effect 78k), infer_from_objects: step 3's handle, not more accessors. The
unmade slot is the source prop's pointer with the low bit set; the mask in ty_get_prop()'s scan costs ~8M
instructions on drizzle. Tried and dropped: re-reading the slot on a hit instead of keeping a copy (+0.02..0.04%); a
Members-level "all made" flag has no free bits and would cost the same load and test on the indexed path.

**The signature census for step 4** (instance Signatures made by instantiate_members(), K1, counter build of
c28559e09; touched = read out of a call/construct array or resolved):
| | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| instance Members with signatures | 220k | 0.9k | 98k | 72k | 87k |
| instance Signatures made | 248k | 1.4k | 107k | 75k | 88k |
| resolved (signature_resolve(): SigResolved + params made) | 243k (98%) | 1.3k (97%) | 88k (82%) | 66k (87%) | 80k (90%) |
| return type computed | 225k (91%) | 1.1k (81%) | 82k (77%) | 59k (78%) | 72k (82%) |
So instance signatures are nearly all used: making them lazily like props would save 2-18% of 104 B each (imm ~0.5 MB).
Step 4's bytes are in their size, not their count: a thin per-instance record (the three words, flags, the view)
instead of a 104-byte copy, and SigResolved's target/mapper copies; imm's 33 MB of SigResolved and parameters are
per-instance content that only lazily instantiated parameter types (bun's) would shrink.

**Expected gains, honestly.** The cycles the bytes can buy are bounded by the memory-bound share (laptop top-down:
imm 31%, store 43%, the corpora 25%) times the part of it that is graph bytes (imm: 74% of L1D misses, 57% of DRAM
fills fresh memory). Guesses to be measured, K1 cycles: step 1 zod -2..-4%, drizzle -1..-2%, imm and effect ~-1%;
steps 1-3 zod -4..-7%, drizzle -3..-5%, effect -1..-3%, imm -2..-4%; with step 4 imm -4..-8%; store ~-1% (its stall
is the intern tables'). RSS follows the arena: K1 zod ~-25%, drizzle ~-18%, effect ~-10%, imm ~-15% after step 4, and
at the defaults each checker's arena shrinks alike. On zod/drizzle/effect tttt already runs half bun's instructions
and cycles with the same top-down profile: there the gain is mostly memory, not wall. On imm (bun 30% faster) this
closes perhaps a third of the gap; the rest is front end (+150 M cycles) and bad speculation (+59 M). It does nothing
for the per-checker duplication (hub_regime.md: ~1 + 0.25 K types at K checkers), which is the threads question.

## 5. Resolution-order invariants
What must stay eager to keep diagnostics where they are:
- instantiate_members() keeps instantiating index infos, the mapped info (`ty_mapped_info_instantiate()` pushes
  RESOLVING_BASE_CONSTRAINT and reports TS2313) and inherited bases (`builder_add_inherited()` resolves base members;
  probe_eager_bases()' TS5114/TS5115 runaways) at the same moment, in the same order. Only allocation moves.
- A prop's type is computed on the same first read as today, by the same function (instantiated_prop_type()), with
  the same in-flight rules: SW_LOOK_MINE re-instantiation (KEY_PROP_REENTERED_TYPE, the 200 backstop), RESOLVING_PROP
  pushed only for non-instances and mapped props, TY_CIRCULAR on a cycle. No step computes a slot when a view is made.
- Signature resolution keeps its sig_resolving frames and partial records (cycles read what is resolved so far).
- No identity class changes: (view, index) equality is today's instance Prop equality; the thisless share and
  materialized props keep theirs. tsc's history-dependent shortcut (the declared symbol when its type is resolved
  and free of type variables) is not adopted.
- Union order and deep-nesting cuts use type ids, not prop addresses: allocation order of Props is not observable
  (sg2's audit: id-keyed caches and intern keys aren't).
The tests that catch a slip: the suite's symbols and types components (decl pointers, annotation reuse:
display_structure's `prop != declared`), the 34 `test/cases` circularity/reentry cases (`ls test/cases | grep -iE
'circular|cycle|2615|2502|7022|7023|reentr|recurs'`), kdiff K1/K16 and buncases' K-dependent column (bun 92-R16b's
TS2615 pair, 93-R01d; the 99-V4 family of generated self-referential Box interfaces), checkerdiff with the shared
graph (lazily materialized props are its first new post-publication identities), mutate.py on mapped/accessor tests.

## 6. Downsides and what stops the work
| downside | mitigation |
|---|---|
| Indirection: a branch on `at` per type read, a 16-byte handle | one inline accessor, materialized props unchanged; instructions gated per step (+0.5% unrepaid stops it) |
| The conversion (step 2) is large: ~120 functions, ~1300 field reads | accessors named after today's fields, compile-enforced renames, no behaviour change in that step |
| Semantics traps: an instance's decl is the zeros, its target the source; this-only shares | the accessors reproduce them exactly; simplification only in step 5, each its own gate |
| Lazily materialized Props are post-publication identities (shared graph) | CAS into the array slot before returning (no escape) |
| Packing the index into the pointer's top bits | panic if a view's address needs them; shapes over 65535 props stay materialized |
| Tools keyed by Prop* (fingerprint.inc, taint.c, graph_debug.c, survivors.c, hub_trace, iterate.c's mapper chains) | converted with step 2; their oracles (taintstats, checkerdiff) in its gate |
| Gains on ordinary corpora are bounded (memory is a quarter of their cycles) | stated per shape above; RSS is the dependable win |
**Stop rules**: after step 1, the laptop's protocol on zod and drizzle: if page faults and DRAM fills don't fall by at
least half the arena's share and K1 cycles by >= 1% on zod, stop before step 2 (the big conversion) and take the
intern tables instead. After step 3: if imm's L1D misses and DRAM fills fall less than 5%, skip step 4 for imm-like
shapes. Any step whose instruction rise isn't repaid in cycles on the laptop is reverted.
