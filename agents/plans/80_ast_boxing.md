# Plan 80: AST boxing, spans, source text (the mechanical memory items)
Brief by the insights of 2026-10-16 (vision.md "Memory target"). Gate per item: **>= 50 MB on vscode after names, or
trivially cheap**. Executed 2026-10-17 (todo entry 5): **closed**. The items were sized before plan 78 deferred function
bodies; re-measured after it, only the source text passed.

## What plan 78 changed (vscode, `--checkers 1`, a census walk after names and at each first materialization)
- After names the AST holds **1.32M Exprs** (95 MB: 371k identifiers, 186k literals); the bodies hold 6.87M more, but
  they exist only while materialized (a few hundred MB live at once at most). The malloc heap after names
  (`MEMTRACK_STOP=resolve_names scripts/memtrack.sh`): AST ~470 MB (50% zero words, diffuse: class member vectors 61 MB,
  import/export statements ~67, parameter vectors ~47, statement lists 33, interface members 23), source text ~175,
  the ResolvedModule cache 33.
- Sizes (bytes): Expr 72 (IdentifierExpr 40, Value 32, the vecs 24), ClassItem **448** (an inline FunctionImpl 288),
  FunctionArg 200, PortStatement 216, PortItem 120, ModuleRef 112, InterfaceMember 152, TypeNode 64.

## Items
1. **Expr 72 -> 40: no-go.** After names -32 B x 1.32M = -42 MB of slots, plus +56 per identifier box and ~+32 per
   literal: **~-15..20 MB**, and the same ratio on live materializations; a pointer chase per identifier.
2. **Fat unions: no-go each, leads kept.** ClassItem 448 -> ~312 by boxing the method's FunctionImpl (147
   `.method.func`/`.constructor.func` sites) is ~-17 MB; boxing a field's value and type too ~-25 MB. FunctionArg's
   InitializedBinding, PortStatement/ModuleRef: ~10-20 MB each. Together maybe 60-80 MB for several hundred sites
   (bugs/perf_leads.md "Memory").
3. **Span 16 -> 8: no-go.** ~15% of the after-names AST is spans: ~-35 MB for ~650 sites, the binder's and checker's
   copies unchanged.
4. **Source text: done.** Files mapped (MAP_PRIVATE) into reserved segments and their pages dropped from the process
   after names and at every checker recycle (documentation.md "Load from the filesystem", hacks.md
   `source_text_map()`). Not just anonymous memory: RSS, because a recycle drops what the last segment read. vscode
   (best of 3 against 8116490e): K1 peak 1616 -> 1518 MB, the defaults 1888 -> 1775; wall +0.9% / +1.2%.
   Also the ResolvedModule cache is dropped after names (nothing resolves later): -30 MB after names and at the peaks.
5. **Per-file LazyType slots**: not done (plan 78 didn't take them; hacks.md "LazyType slots" still holds).
