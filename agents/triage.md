# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-07 (triage session, todo entry 10 of the eleventh window): four Opus sub-agents (type core,
binder/modules, display/text, parser/missing checks) took every item, then second tasks (fresh mutation seeds 16
multi/js and 17, the last vscode error, flagflip leftovers, errtextdiff). ~70 fixes; the rest written up in
bugs/type_core.md, binder_modules.md (new), display.md, harness.md, missing_checks.md, tsgo_divergences.md (new),
plans 26/27/39 Leftovers and the new plan 50 (tsc's export= / CommonJS symbol clones); the corpus re-pin configs moved
to plan 19. The commit message has the numbers.

- Perf (plan 48 stage 6, 2026-10-07; todo entry 6 of the twelfth window takes it): vscode is tttt's weakest
  ratio against tsgo (3.0x; 6.2 s = load+parse 1.0 + resolve 0.7 + typecheck 4.3). Two structural levers, both known:
  the four checkers redo ~24% of the type work (3.19M types vs 2.57M with one checker; plan 36's sharing question),
  and the AST is ~2 GB resident for 3.41M lines + 350k of .d.ts (~600 bytes a line; plan 43 was the last pass).
  `-b` on tsc's src is 2.2x tsgo, with 182 ms of `free` summed over the projects at `--build-jobs 1`.
