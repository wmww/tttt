# How `bun check` parallelizes its checker (read from its source)
Read-only study, 2026-10-22, of oven-sh/bun `main` at 38afee6c8 (2026-10-22; the benchmarked canary is
1.4.3-canary.20261019.1, three days older). The checker is the Rust crate `src/sema` (~200k lines): `check/` (the
ported tsgo checker), `types.rs` (the type store), `table.rs` + `local.rs` (shared and task-local result tables),
`check/commit.rs` + `check/task.rs` (the concurrency protocol), `driver/lib.rs` (scheduling), `program.rs` +
`components.rs` (front end). Paths below are relative to `src/sema/`. Nothing was run; numbers are only those the
source states. shared_graph.md's last section has an earlier, shorter reading at 6bb5dc19; this one goes further.

## Summary
bun has **one type store for the whole program** and **checkers that are short-lived tasks**, with a database-style
protocol between them: records are hash-consed and published at creation, query *results* are written into a
task-local buffer and published in numbered **commits** (first entry per key wins), and a task whose results turn out
order-dependent is **discarded and its file rechecked** (optimistic concurrency, rollback per file, a serial last
round for leftovers). Determinism comes from removing type-id order from every observable decision (a content order
replaces it) plus program-order rules about which task may be trusted with a cycle or an error, not from tracking
which values are "clean" before publishing. What is shared is chosen by locality: results about the visited file's
expressions and about records built from the visited file's syntax (by bun's own count "nine out of ten entries")
never leave the task. Variance measurements are the one place threads wait for each other.

Against tttt's parked shared graph this is the design it rejected as (B) ("immutable types with per-worker lazy
tables published at quiescent points"), made affordable by three things tttt's version didn't have: dense
id-indexed tables (a shared hit is one load), per-task contiguous id ranges, and rollback instead of taint.

## 1. Unit of parallelism and scheduling
- **Unit: a task = a run of consecutive files in program order, on one thread, with one fresh `Checker`**
  (`check/task.rs` header). Each file is checked whole (`check_source_file.rs`, `checkSourceFile` order); there is
  no per-declaration or per-statement parallelism. A task commits after every file and, within a file, at its first
  idle moment (empty query stack) after logging 1024 keys (`commit.rs` `KEYS_PER_COMMIT`).
- **Task size** (`driver/lib.rs` `MIN_TASKS = 128`, `TaskSize::ByCost`): a checker is replaced after 1/128 of the
  program's total cost "because its private tables grow with every file". A task also ends when the next file it
  would take is earlier in program order ("a task only moves forward in program order").
- **Cost estimate** (`driver/lib.rs`, `cost_of` in the task runner): identifiers in expressions + type nodes + 1 per
  file. The comment: on storybook, partitioning by text size gives tasks 1.8x as long as partitioning by measured time,
  this estimate 1.1x; next.js has a 2.9 MB `.d.ts` taking 23% of the check's instructions, hence type nodes.
- **Queue** (`run_tasks` closure in `driver/lib.rs`): heavy files (cost >= task cost) first, largest first, each a run
  by itself; then the rest in program order "because neighbors use the same types". A thread takes a *run* whose cost
  is `cost_left / (threads * RUNS_PER_THREAD=4)` (runs shrink toward the end: guided self-scheduling), under one
  mutex. A thread that finds the queue empty **steals the second half of the longest unstarted run** of another
  worker (`steal_run`). Discarded files go to a `lost` list and are rechecked by whichever worker frees up
  (`take_file_to_recheck`, first in program order that is worth it); once any file is discarded, one worker switches
  to the first unchecked files of the program so cycles everything uses get resolved early (`first_files_worker`).
  Workers with nothing to do wait on a condvar while a recheck may still become possible.
- **Threads**: one per logical CPU. `bun_core::get_thread_count()` (`src/bun_core/util.rs`): `UV_THREADPOOL_SIZE`
  or `GOMAXPROCS` if set, else WTF's `numberOfProcessorCores()` = min(online processors, affinity mask, cgroup quota),
  clamped to >= 2. `--threads N` overrides (`src/runtime/cli/check_command.rs`; docs/runtime/check.mdx: "one thread
  per CPU core"). Threads come from Bun's pool, with 8x its stack (`WORKER_STACK_SIZE`); each checker gets a fixed
  3 MB stack budget so "out of stack" is a function of the program (`check_source_file.rs` `TASK_STACK`).
- So "checkers" are not a fixed K: at any moment there is one task per busy thread, and 128+ checkers are created
  over a run, each reusing its thread's previous one's memory (`Spare`, ~20 KB boxed, `check/mod.rs`).

## 2. The type graph: one, shared
- **Records** (types, signatures, mappers, component lists, atoms) are **hash-consed: equal keys, equal ids**
  (`types.rs` header). An object type records its *origin* (declaration, syntax node, mapper for the enclosing type
  parameters), not its members; members, signatures, constraints etc. live in result tables keyed by the type's id.
  `TypeRecord` is <= 64 bytes; its hot facts are a separate `Summary` array (two per cache line).
- **Interning across threads** (`types.rs` `Interned`, `util.rs` `WindowedSlots`): lookups are lock-free
  (`find`, acquire loads); inserts take one of **64 striped locks**. The hash index is split into **windows by the
  youngest id the key mentions** (64 consecutive ids per window), so recent windows are small and adjacent, and one
  big table's "nearly every lookup is a cache miss and a page walk" is avoided. A creator writes its record into an
  id it reserved, then `publish_record` does find-or-add under the window's lock; a racing loser gets the winner's
  id back and its slot is orphaned. **Ids are reserved per task in ranges** (`reserve` under a lock) because "a single
  counter shared by all threads would be written on every creation. Ids from different threads would also interleave,
  so every table keyed by id would have cells from several threads in one cache line." Ids are therefore
  schedule-dependent, so nothing may depend on id order (see 3).
- **Task-local records** (`types.rs` `OwnStore`, `Sharing::Finds/Publishes`): a record that mentions a node or symbol
  of a file nothing imports ("bound": `Module::is_leaf`) is never published; it gets an id with the `LOCAL` bit,
  indexed densely within the task (`local.rs`). The shared part of a table never has a key containing a `LOCAL` id,
  so a task's private objects can't escape into shared data. A checker outside a task (services) never publishes.
- **Result tables** (`table.rs`): `ByNode` (keyed by file + node number, or symbol), `ById` (keyed by type/sig id),
  `ByKey` (hashed, e.g. `instantiations` keyed by (type, mapper), `relations`, `composed`). Dense arrays of 32-bit
  atomic cells, zero = not computed, written once; the memory comes zeroed from the OS so untouched pages cost
  nothing. Three policies: `Frozen` (front end; CAS, first wins), `Buffered` (checker results: task-local part +
  shared part, published at commit or immediately), `FileLocal` (dropped at the next file). `get` on a published
  entry is one load; the task-local part (dense, no hashing, `local.rs`) is consulted only if the shared cell is empty.
  `Program`'s fields in `check/mod.rs` declare 75 `Buffered` tables: `expr_types`, `symbol_types`, `declared_types`,
  `members`, `shapes`, `sig_params`, `resolved_return_types`, `instantiations`, `relations`, `base_types`, `calls`...
- **What stays private** (`table.rs` `stays_in_task`, `Location::KEEPS_VISITED_FILE_LOCAL`, `commit.rs`):
  - results about expressions of the file the task visits ("most of what a check stores, and other tasks rarely look
    it up"); another task reaching such an expression on the way to a declaration's type shares *its* entries;
  - results keyed by a record made from the visited file's syntax ("Nine out of ten entries are never read by another
    task, and they're most of what a check holds at its end");
  - results that depend on how deep the checker was: base constraints cut short, relations that succeeded via
    `isDeeplyNestedType` or the 100-level limit, and what follows from them (`constraints_of_this_task`,
    `relations_of_this_task`, `check/mod.rs`);
  - `check/links.rs`'s per-checker cache of derived parts (conditional/mapped type pieces).
- **Lazy computation in flight elsewhere: duplicate, don't wait.** A task never sees another's in-flight state; if a
  shared cell is empty it computes the result itself into its buffer, and at commit the first entry per key wins
  (`commit.rs` "One commit at a time"). The loser's entry and the diagnostics reported under it are dropped
  (`Program::drop_diagnostics_of_losers`, `sink.rs`). Results keyed by records may be **written immediately** (not at
  commit) when nothing they depend on waits for a commit (`Task::may_write_immediately`, `hold_back` counter); after
  anything that might get the task discarded or a cycle result, immediate writes stop until the next commit.
- **Variances: claim and wait** (`commit.rs` `Claims`, `Program::claim_variances`, `relate.rs`
  `share_measured_variances`). "A measurement can take seconds, and its result is just a few flags ... So one thread
  measures, the others wait, and the result is shared when the measurement ends, not when its task commits." A thread
  claims a symbol before an outermost measurement; others block on a condvar. **No deadlock by construction**: a
  thread waits only if it is not measuring, and holds a claim only while measuring, so every wait is on a running
  thread that holds no lock a waiter needs. A result is not shared if the measurement reported a diagnostic, used an
  order-dependent note, or ended open (cycle/cut); after failed claims a symbol is "given up" and everyone measures.
  Unshared values are recorded as `OrderDependent` and compared at commit with `serial_variances`; a contradiction
  makes the commit invalid (`Program::is_invalid`).
- **Circularity stacks** are per `Checker` (= per task); there is no thread-local state ("There is no thread-local
  state", `check/task.rs`).

## 3. Determinism
The rules are in `check/commit.rs`'s header (the most important file to read); the essentials:
- **No id order anywhere observable.** `Types::content_order` (`types.rs`): rank = (height, last syntax mentioned,
  content hash with ids replaced by what they denote), total by `compare_contents`; it replaces tsgo's id tie-breaks
  (`CompareTypes`'s last key, the swap in `getRelationKey`). `isDeeplyNestedType`'s "t.id >= last.id" becomes
  `nesting_order` (heights) then push order on the relation stacks (`relate.rs` `is_at_least_as_new`). Atoms are
  numbered in parse order and "never order anything by atom number". These are documented approximations of tsgo
  (creation order ~ syntax order), i.e. parity is given up in edge cases.
- **Commits are serialized and numbered** (one mutex). Record-keyed entries are stored in log order with release
  stores, then node-keyed entries stamped with the commit number, then `stored` is bumped; a task sees a node-keyed
  entry only if its commit is <= the snapshot it took of that file (`View::snapshot_of`), and the visited file is
  pinned for the whole visit (`View::pin`): "a file doesn't change under a statement".
- **Cycles are entered from the first file that leads to them**, as with `tsc --singleThreaded`: a task that closes a
  resolution cycle is trusted only if every earlier file in program order is final (or only importers can reach it);
  otherwise it is discarded and its file rechecked once a commit resolves that cycle (`Task::close_cycle`,
  `Program::is_cycle_resolved`). "A cycle is seen whole or not at all": cycle commits announce themselves first and
  overlapping tasks check whether they missed a member (`discard_if_missed_cycle_member`).
- **Limits**: a task that gives up at a limit (instantiation depth, 2589, ...) is discarded and its file waits for the
  **last round**: one task, one thread, the leftover files in program order, seeing every commit, trusted.
- **Errors**: a diagnostic belongs to a query entry and travels with it; the first committed entry's diagnostics are
  reported, losers' dropped. A task reports only in its own and later files; tasks ignore what a later file's task
  stored under a file where an error was found (`View::hides`), so the first task in program order decides, as in tsc.
  `check/check_order.rs` replays `checkSourceFile`'s visiting order from syntax to place once-per-file errors (JSX
  factory, tslib helpers) independently of what other tasks cached. Each file's diagnostics are buffered (`Sink`) and
  finished only after the last task ends (`Program::finish_file`, `driver/lib.rs` `finish_files`).
- Tests: `PlanOptions::task_size` (`OneFile`, `Unlimited`) and `discards_first_visits` exist to show the output is
  independent of them.

## 4. Memory (why ~2.2 GB on vscode)
Inferred from the design; the source gives no breakdown.
- **One copy of everything shared**: lib and declaration-file types, instantiations, members are built once, not
  per checker (tttt at K24 holds up to 24 graphs, each with its own lib types and common instantiations).
- **Most results are task-local and freed with the task** (the "nine out of ten" entries; expression types of the
  visited file); tasks end every 1/128 of the cost, and the next task on the thread reuses the memory (`Spare`).
- **Arenas** (`session.rs`): one mimalloc heap per thread, created on first use, all freed together at the end; blocks
  can be freed individually (so dropped containers return memory), any thread may free any block. Task-local stores
  are on the regular heap and freed by the thread that ran the task. Idle pool threads release their free pages
  (`release_free_pages_of`).
- **HIR, not an AST**: per-kind vectors with index references, "a handful of allocations" per file, immutable after
  parse (`hir.rs`); derived per-file data (parents etc.) computed on demand (`hir::Lazy`). The HIR of a leaf file
  (nothing imports it, it declares no globals) is freed at the end of its task (`Files::free_tree`), which is why bound
  records are never published.
- Dense tables on zeroed OS pages: an untouched page is never resident.

## 5. Front end
- **Load = parse + bind per file, on all threads** (`Files::load`, `program.rs`), with an IO pool for reads; each file's
  HIR lives in the arena of the thread that loaded it. Within one request, programs share files parsed once
  (`program::Run`, keyed by path and `ParseKey`); `bun check` shares only declaration files, `bun lint` everything.
  Nothing is cached across processes. There is no deferred parsing of function bodies (bodies are parsed into HIR).
- **Merge** (global symbol merging) runs on one thread while side jobs run in parallel: the import graph's strongly
  connected components (`components.rs`) and the end of module resolution (`host.parallel_while(side_jobs, ...)`).
- **Link** (exports, aliases) runs **per SCC component in parallel, by level, without a barrier**
  (`Files::link_components`, `LinkPlan`): a component's task reads only its own results and those of lower-level
  components (waiting for them if needed), recomputes anything else itself, and publishes for its own symbols at once;
  other results are deferred and merged first-wins in level order. A chain of modules is linked by one thread that
  never waits. No timings are given in the source.

## 6. Relative to tsgo, and what applies to tttt
**Kept from tsgo**: the checker's algorithms, function by function with tsgo names in comments (`lib.rs`: "Large parts
of `check` and `bind` are ported from typescript-go's checker and binder"), `checkSourceFile`'s visiting order, the
query semantics (cycles, limits, variance measurement, deferred diagnostics).
**Changed**: tsgo has K independent checkers (4 by default in the CLI) with files associated statically by weight and
adjacency (`tsc/internal/compiler/checkerpool.go`); bun has one store and 128+ short tasks on a dynamic queue. tsgo's fields on
Type/Symbol objects ("links") became id-keyed side tables with buffered/shared halves; types became hash-consed
records with origins; id order became content order (parity cost); determinism comes from commits, discards, rechecks
and a serial last round. HIR replaces the AST.

**What would apply to tttt**, with honest estimates:
1. **Cost estimate by identifiers + type nodes instead of source bytes** for dealing, and costliest-first plus
   shrinking runs (low risk, an afternoon). tttt deals by size (`deal_files()`); bun measured size as a 1.8x worse
   predictor than this on storybook. Gain: the tail of the check phase on projects with skewed files; a few percent of
   wall at most, since tttt's check phase is already ~80% efficient. Worth it as a measurement first
   (`TTTT_FILE_TIMES` against the two estimators).
2. **Parallel link by SCC level** (medium risk, a few days). vscode's link is ~40-50 ms serial at every thread count
   (front_end.md); bun's scheme (read only lower levels, recompute the rest, publish own symbols, first-wins merge for
   the rest in level order) is deterministic by construction. Gain ~30-40 ms of vscode's ~1.9 s and proportionally
   more on small projects where the front end is most of the wall (nest).
3. **If sharing is reopened, bun's choices answer the measured problems of tttt's shared graph:**
   - *Cross-core line traffic* (tttt's 1.2-1.6x per-instruction premium): bun avoids writing shared lines that others
     read hot: ids reserved per task in ranges (each thread's table cells in its own lines), records published once at
     creation (the only later writes are idempotent fact bits and a rank in a separate `Summary` array), results published in
     batches at commits rather than by a CAS per word, and 90% of entries never published at all.
   - *The machinery cost* (tttt's bucket (d), 43-61% of the excess, ~150-300 instructions per shared probe after a
     private miss): bun's shared hit is one load in a dense id-indexed array; the private miss path is a dense
     task-local array, no hashing. tttt rejected (B) because "a table probe on every lazy read costs 4-6x the words'
     fast path"; bun pays that and is 1.5-2.5x slower than tttt single-threaded, so this is likely part of why.
   - *Variance groups* (48-60% of drizzle's and zod's excess, tainted by deep-nesting cuts that depend on id order):
     bun shares a measurement unless it reported a diagnostic or used an order-dependent note, because its deep-nesting
     test no longer uses ids; and one thread measures while the others wait, deadlock-free by the "only non-measuring
     threads wait" rule. For tttt the wait rule is directly portable (tttt's rejected (A) waited inside computations;
     bun waits only outside any measurement). The id-free nesting test is not: it changes output against tsgo where
     creation order and height disagree, which tttt's oracles would show.
   - *The overall bet* differs: tttt's graph published only values proven clean (taint on every read, POISON,
     shadowing); bun publishes everything not depth-dependent, then detects order dependence at commit and **rolls back
     the file**. Rollback needs task-local buffering of every result write (tttt writes into objects) and a recheck
     scheduler; it is the large, high-risk part. Expected gain if fully done: tttt's measured ceiling (duplicated
     instructions 8-12% on vscode, 54-63% on drizzle) minus a premium bun's layout suggests can be small but tttt hasn't
     shown; vscode would not benefit (its ceiling is under the premium already), drizzle-like generic-heavy projects
     and the recycle thrashers would.
4. **Short tasks with a fresh checker each (1/128 of cost)** only make sense with a shared store; with per-checker
   graphs they would multiply warm-up (tttt's recycle already costs 15-20% on vscode). Not applicable alone.
5. **Memory**: bun's 2.2 GB vs tttt's 5.4 GB on vscode at the defaults is mostly "one graph instead of 24" plus
   freeing task-local results and leaf HIR. Without sharing, the transferable parts are freeing a leaf file's AST
   after its check (tttt keeps ASTs read-only and shared) and keeping the visited file's expression results out of
   anything that outlives the file (tttt's memo slots per file already page by file).

**Doesn't apply**: content order and the id-free nesting test (parity with tsgo is tttt's oracle), the program-order
error rules (`View::hides`) and the last serial round as long as tttt's checkers don't share results (tttt's output
is already K-independent without them), and bun's HIR (tttt's AST is fine for its single-thread speed).

## Three facts that matter most for tttt
1. **bun shares selectively and in batches**: records once at creation (striped locks, per-task id ranges), results
   at per-file commits; expression results of the visited file, records from its syntax (~90% of entries) and
   depth-dependent results never leave the task; a shared hit is one load. tttt's shared graph shared every word with a
   CAS and paid on every probe.
2. **Determinism is by rollback, not by clean-only publication**: first committed entry wins, order dependence
   (cycles entered from a non-first file, contradicting variances, limits) is detected at commit, the file is rechecked
   later or in a serial last round; id order is removed from every decision (content order), at a parity cost.
3. **Variances are claimed and waited for, deadlock-free** (only threads outside any measurement wait; claims only on
   outermost measurements; results are a few flags, shared at the end of the measurement). This targets tttt's largest
   excess bucket and is the one piece portable without adopting the rest.
