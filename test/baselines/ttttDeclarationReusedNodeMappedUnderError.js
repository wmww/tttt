//// [test/cases/ttttDeclarationReusedNodeMappedUnderError.ts] ////

//// [ttttDeclarationReusedNodeMappedUnderError.ts]
// A method whose parameter's type is an error (an unresolved import in its union) is still instantiated: tsc's
// isTypeParameterPossiblyReferenced is syntactic, so the reused annotation prints the mapped type parameter (by its
// type, the innermost failing node's recovery), not the declared one renamed
import type { Missing } from 'nonexistent';
type Common<S> = { dispatch(action: Missing | ((state: Readonly<S>) => Missing)): void };
export function a<State>(h: Common<State>) { return h.dispatch; }
type Common2<State> = { dispatch(action: Missing | ((state: Readonly<State>) => Missing)): void };
export function b<State>(h: Common2<State>) { return h.dispatch; }


//// [ttttDeclarationReusedNodeMappedUnderError.js]
export function a(h) { return h.dispatch; }
export function b(h) { return h.dispatch; }


//// [ttttDeclarationReusedNodeMappedUnderError.d.ts]
import type { Missing } from 'nonexistent';
type Common<S> = {
    dispatch(action: Missing | ((state: Readonly<S>) => Missing)): void;
};
export declare function a<State>(h: Common<State>): (action: Missing | ((state: Readonly<State>) => Missing)) => void;
type Common2<State> = {
    dispatch(action: Missing | ((state: Readonly<State>) => Missing)): void;
};
export declare function b<State>(h: Common2<State>): (action: Missing | ((state: Readonly<State>) => Missing)) => void;
export {};
