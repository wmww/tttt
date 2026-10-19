// @declaration: true
// @strict: true

// A property whose initializer the pseudochecker can't type (a conditional) prints the literal's own property type
// when the widened union is identical to what widening the expression gives: no `reason?: undefined` (graphql-inspector)
export enum Level { A = 'A', B = 'B' }
declare const safe: boolean;
export function f() {
    return { c: safe ? { level: Level.A, reason: 'x' } : { level: Level.B }, type: 'X' } as const;
}
export function g() { return { c: safe ? { level: 1, reason: 'x' } : { level: 2 } }; }
export const h = () => ({ c: safe ? { level: 1, reason: 'x' } : { level: 2 }, d: safe ? { a: 1 } : { b: 2 } });
// Widened from a variable: normalized
export function k() { const r = { c: safe ? { level: 1, reason: 'x' } : { level: 2 } }; return r; }
export let x = { c: safe ? { level: 1, reason: 'x' } : { level: 2 } };
// Not a union once widened, or no object literal in it
export function m() { return { c: safe ? {} : 'x', d: safe ? [{ a: 1 }] : [{ b: 2 }] }; }
export function n() { return { c: safe ? { a: { p: 1 } } : { a: { q: 1 } } }; }
