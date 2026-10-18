# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-18 (triage session, todo entry 9 of the nineteenth window). The five items: deferral restarts
(inbox-zero's apps/web and 17 more source sweep projects; none left) fixed along with a signature suggestion bug they
hid; the symlinked-directory double load is package ids (exports maps now give one; a HACK covers the ordering, plan 84
the real fix); tttt-oracle neither reads nor writes build info any more (851 stray `.tsbuildinfo` files deleted from
thirdparty/sources); TS5115/TS2589's checker dependence is tsgo's own (bugs/tsgo_divergences.md); the harness's leak
list is empty again (materialized bodies walked before their drop under the counting allocator). inbox-zero's remaining
gap to bun is per-checker re-derivation (bugs/perf_leads.md).

