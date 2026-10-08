# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-08 (triage session, todo entry 10 of the twelfth window): scripts moved to `scripts/` (the
human's request); the human's TypeScript 7 request researched and written up as plan 53 (+ 53_findings.md); plans
36-42, 44, 45, 48, 49, 51 consolidated into bugs/, documentation.md and hacks.md and deleted; Opus sub-agents fixed the
vue order dependence (alias variances), discord.js's global-block merge order, unknown compiler options
(TS5023/5025/5093/6369/6114/6258), the root-file "in the program because" chain and TS6305 for `/// <reference
path>`, the lone-`\r` harness split, the mikro-orm spread constraint and several source-sweep keys, then second
tasks (a mutation seed, the kysely perf lead, svelte/react-navigation). The commit message has the numbers;
leftovers are in bugs/include_order.md, harness.md, sourcesweep_checker.md, missing_checks.md, perf_leads.md.
