# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `type_pair_cache.h`: relation cache keyed by a string rendering of both types (`hash_key_display`). Slow and
  fragile (two distinct types that print the same collide). Goes away with the type core redesign.
- `type_pair_cache_entry_update`: uses a key whose ownership was already handed to the map ("hacky but it works").
- `baseline_test_case.c: load_test_source`: compiler option directives handled as ad-hoc string compares; almost
  all options ignored.
- `program.c`: `.js` import path handling by suffix check - not real module resolution.
- ~60 `PERF`/`HACK`/`TODO`/`FIXME` comments in `src/` not yet catalogued here (triage).
- `failstats.py: line_ratio`: "line match" is multiset overlap of lines, not a diff (difflib was far too slow), so
  it overstates similarity when lines are reordered. Good enough for ranking nearly-passing tests.
- `failstats.py: syntax_gaps`: locates the offending source line by re-parsing tttt's rendered errors output;
  approximate for multi-file tests.
- `bench/make_corpus.py`: corpus is passing tests wrapped in namespaces and replicated. Flatters tttt.
- `main.c: --keep-going`: typechecks after name errors purely so the bench can time the stdlib; not validated.
- `port_item_resolve_alias` (`import x = A.B`): the alias is a *copy* of the target declaration, made during name
  resolution by overwriting a placeholder in place (other ports copy too, see `process_port`). An alias to something
  declared by a later alias, or to a namespace that gains members later, sees a stale/any copy. Wants real symbols
  with alias links (plan 03). `alias_conflict` likewise only approximates merging an alias with an existing name.
- `parse_import_equals`: `import x = require("m")` is treated exactly like `import * as x from "m"`. Wrong for
  `export =` modules (not supported at all yet).
- `namespace_decl_add_names`: ambient modules (`declare module "m" {}`) get a scope for their contents but are not
  registered anywhere, so `import ... from "m"` still reports TS2307. Needs a program-level ambient module table
  (module resolution work). Their `"m"` name line is also missing from the symbols/types baselines.
- `Interface.namespace_scope`: namespace value types print as `typeof A.B` via a borrowed `Scope*` stuffed into the
  structural interface; without it, display (and the string-keyed relation cache) recursed forever on
  `namespace A { export var a = A }`. Any other structural walk over such a type (eg `type_extends`) is still only
  saved by the recursion depth limit. Type identity in the new type core is the real fix.
- `parse_jump_target` / `Statement.jump_label`: labels are parsed and shown in the types baseline, but never checked
  (TS1116 unknown label, TS1107 crossing function boundary, TS7028 unused label, duplicate labels). `inside_label`
  in `BlockTypecheckCtx` just permits any labeled `break` inside any labeled statement.
- `run_baseline_test_case: path_is_script`: virtual files that aren't TS/JS by extension are left out of the program
  entirely. Right for package.json/tsconfig.json, wrong for `.json` modules under `resolveJsonModule` (5 tests).
- `EnumDecl.is_const`: recorded, nothing uses it.
