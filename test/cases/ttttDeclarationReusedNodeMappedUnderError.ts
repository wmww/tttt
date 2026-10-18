// @declaration: true
// @strict: true
// A method whose parameter's type is an error (an unresolved import in its union) is still instantiated: tsc's
// isTypeParameterPossiblyReferenced is syntactic, so the reused annotation prints the mapped type parameter (by its
// type, the innermost failing node's recovery), not the declared one renamed
import type { Missing } from 'nonexistent';
type Common<S> = { dispatch(action: Missing | ((state: Readonly<S>) => Missing)): void };
export function a<State>(h: Common<State>) { return h.dispatch; }
type Common2<State> = { dispatch(action: Missing | ((state: Readonly<State>) => Missing)): void };
export function b<State>(h: Common2<State>) { return h.dispatch; }
