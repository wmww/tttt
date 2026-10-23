# agents/bugs: status tags
Every top-level bullet of `agents/bugs/*.md` (but `perf_leads.md`: performance, not correctness) and of
`agents/triage.md` ends with one status tag, so `scripts/bugcount.py` can count the known correctness bugs. A
paragraph or a sub-bullet may carry one too (then it is an entry of its own; a bullet whose sub-bullets carry the
tags needs none itself). Untagged paragraphs are context.

| tag | meaning | counted |
|---|---|---|
| `[open]` | a known input where tttt's diagnostics, types or emitted declarations differ from tsgo's | yes |
| `[design]` | open, and the fix needs a design (written up in the entry or a note) | yes, also apart |
| `[text]` | the only visible difference is text: message wording, elaboration chains, printed types (types and symbols baselines, message headers), declaration trivia (comments, maps) | apart (ranked last) |
| `[tsgo]` | an argued tsgo artifact (its harness, emit order, checker count, a runaway it reports by accident) or a divergence declined as won't-fix with its reason | no |
| `[fixed REF]` | history: REF is the fixing commit, a date, `probe DATE` (a rerun probe matches now) or `oracle DATE` (the oracle that showed it matches now) | no |
| `[dup]` | the same root cause as another entry, which carries the count | no |
| `[note]` | no known divergence: a latent mechanism difference with no input that shows it, a pointer, a lesson, a perf or process item | no |
| `[harness]` | the test harness or the tooling, not tttt's behaviour (a harness divergence the CLI doesn't have counts here too) | no |

Rules of thumb: one entry per root cause as far as we can tell; when one bullet repeats another's mechanism, tag
it `[dup]` and keep the count on the entry with the write-up. When unsure between open and fixed, rerun the entry's
probe (`FAST=1 scripts/filetest.sh`) or look for its project in the oracle's `--last` output; an oracle that names
a project the entry was about matching now is `[fixed oracle DATE]`.

Workflow: a fix's commit flips its entry to `[fixed COMMIT]` (or deletes it when nothing in it is worth keeping);
a new divergence gets an entry tagged `[open]` (or `[design]`/`[text]`) when it is written up. The coordinator runs
`scripts/bugcount.py --save` once per landed batch, which appends a dated line to agents/measurements.md "Known
correctness bugs". `scripts/bugcount.py --list untagged` must stay empty.
