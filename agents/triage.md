# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-12 (triage session, todo entry 10 of the fourteenth window): own regression tests wired in
(test/cases), lib files checked like tsgo, the knex variance blow-up fixed, the vscode excludes dropped, GCC 16 /
clang 22 builds clean, the small items filed into bugs/display.md and harness.md, the human's emit answer in vision.md.
The commit messages have the outcome.

## For the insights session
- `AGENTS.md` is a copy of `CLAUDE.md` that stopped being synced on 2026-10-08 (agents edit CLAUDE.md, a few edited
  both). Ask the human whether it's still wanted, or make it a symlink to CLAUDE.md.

## Open
- (2026-10-12, test/cases wiring) A JSX element spreading five unions of ten object types
  (`<Comp {...a} {...b} {...c} {...d} {...e} />`, Comp generic, `@jsx: preserve`) is TS2590 in tsgo, nothing in tttt
  (missed, before and after 16b4bb4b). The same five unions in `inter(a, b, c, d, e)` returning `P & Q & R & S & T`
  match tsgo. Probe: the jsx spread's union/intersection size check.
- (2026-10-12, test/cases wiring) TS2322 elaboration "Type 'SVGCircleElement' is missing the following properties from
  type 'HTMLSpanElement': ..." lists `accessKey, accessKeyLabel, ...` in tttt, `contentEditable, enterKeyHint, ...` in
  tsgo (react16.d.ts, test/cases/ttttDeferredCheckErrorLocation.tsx without its @ts-ignore; tiptap's MarkViewContent):
  the missing-properties list's order or filtering differs (both say "and 26 more").
