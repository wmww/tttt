// @strict: true
// A deferred function body (plan 78) whose signature could have seen what the body declares: the eager walk resolves
// the return type's `typeof local` to the body's local, so the run is redone without deferral (baseline_tests
// --defer-bodies, program_defer_failed())
// @filename: a.ts
export function sees(): typeof local { const local = { a: 1 }; return local; }
export function missing(x: Unknwn) { const Unknown = 1; return x; }
// @filename: b.ts
import { sees } from "./a";
export const a: string = sees().a;
