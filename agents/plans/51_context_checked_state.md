# Plan 51: tsc's contextual-check state instead of side channels
Brief by the insights of 2026-10-07 (end of the eleventh window). A planning session (todo entry 1) prototypes and
turns this into a staged plan, or says no with numbers.

## Why
hacks.md's "Calls and inference" and "Expressions and contextual typing" sections (~140 lines) are mostly one
approximation patched from many sides. tttt instantiates contextual types by pushing the inference mapper down
(type_core.md "contextual instantiation") and remembers "the contextual type a function was last checked with"
in per-node keys (`KEY_CONTEXT_WIDENING`, `KEY_CONTEXT_BLOCKED`, `KEY_CONTEXT_NONE`, `KEY_CONTEXT_RETURN`,
`KEY_FUNCTION_SIG_CONTEXT`, `ty_late_contextual_return`, `ty_function_context_is_self`,
`Checker.overload_failure_inference`, `Checker.implementation_elaboration`). tsc instead has:
- `instantiateContextualType` (leaf-wise: the contextual type is instantiated where it is read, with the inference
  context's current mapper, only for the parts that need it);
- `NodeCheckFlags.ContextChecked` / `checkFunctionExpressionOrObjectLiteralMethod`'s "first Normal-mode check
  decides" rule for context-sensitive functions;
- the contextual signature read through `getResolvedSignature` of the enclosing call (cached per call).
The 2026-10-07 triage added two more narrow patches here (65b6cdd1, bd465667) and flagged the pattern.

## What the planning session decides
1. Prototype (in a worktree, sub-agents welcome) the port of the three mechanisms above, *deleting* the side
   channels rather than adding beside them (plan 47's method: delete first, measure what breaks: suite, corpora,
   sweep, `$ExpectType`, flagflip). Which side channels go away cleanly, which need the port, which survive and why.
2. Cost: instructions on the corpora (tsc reads contextual types lazily; tttt's push-down may be cheaper or dearer).
3. Write the stages (todo has two execution sessions for it: entries 4 and 9), each 0 lost on every oracle.
   If the port isn't worth it, say so here with the numbers, and list which hacks are then permanent.

## Validation
Every hack listed above either deleted or justified in hacks.md; suite 0 lost; corpora 0; sweep, `$ExpectType`,
flagflip not worse; instructions within ~1% or explained.
