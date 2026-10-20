# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-20 (triage session, todo entry 5 of the twenty-third window). A truncated mapped source no
longer SIGBUSes (`text_sigbus()`: zeros over the page, one warning; verified by truncating a 6 MB file mid-run).
pgobuild.sh serializes builds (flock), waits out running pgo/tttt processes and kills its training run with itself
(the corrupt .gcda). dokploy at `kdiff -m 0`: slow, not quadratic (kdiff.py's docstring). perfsweep's summary line
was already fixed (e76f67ca); its wall/CPU gap was load (nuqs docs 0.33 s wall quiet). The bwrap flake went to
bugs/harness.md. measurements.md condensed 2139 -> 408 lines, hacks.md audited (no entry gone, many narrowed).

