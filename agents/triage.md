# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-28 (triage session, todo entry 10 of the fourth window; suite 10516 -> 10543). Fixed: webpack's
checker-count dependence (overload groups keyed by block only; webpack 30 at every K), the baseline walk typing each
file with its own checker, TS2769 candidates printed instantiated, type-predicate narrowing keeping the candidate
(map_type), ~20 `TYF_OBJECT` gates that skipped intersections, explicit type arguments failing a constraint
(candidateForTypeArgumentError, TS2344), TS2375 against union targets, regular object literals (evolving arrays of
objects, the `literal_lacks_prop_of` HACK gone), TS7010/7011/7018 from reportErrorsFromWidening, tsc's line breaks
(`\r`, U+2028/9) and whitespace (NEL, U+200B). Routed: eager contextual instantiation (plan 37 F's return_only probe)
to `bugs/type_core.md`, the zod-without-variance TS2589, intersection property lists, spread identity, and the
smaller leftovers to `bugs/*.md` and hacks.md.
