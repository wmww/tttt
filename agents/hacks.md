# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `type_pair_cache.h`: relation cache keyed by a string rendering of both types (`hash_key_display`). Slow and
  fragile (two distinct types that print the same collide). Goes away with the type core redesign.
- `type_pair_cache_entry_update`: uses a key whose ownership was already handed to the map ("hacky but it works").
- `baseline_test_case.c: load_test_source`: compiler option directives handled as ad-hoc string compares; almost
  all options ignored.
- `program.c`: `.js` import path handling by suffix check - not real module resolution.
- ~60 `PERF`/`HACK`/`TODO`/`FIXME` comments in `src/` not yet catalogued here (triage).
