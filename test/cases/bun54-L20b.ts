// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "strictNullChecks": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean;
function g1({ kind, payload } = cond ? { kind: 'A' as const, payload: null } : { kind: 'B' as const, payload: 's' }) {
  if (kind === 'A') { const t: { x: 1 } = [payload]; payload.foo; }
}
function g4({ kind, payload } = cond ? { kind: 'A' as const, payload: [] } : { kind: 'B' as const, payload: 's' }) {
  if (kind === 'A') { const t: { x: 1 } = [payload]; payload.push(1); }
}
class K { m({ kind, payload } = cond ? { kind: 'A' as const, payload: undefined } : { kind: 'B' as const, payload: 's' }) {
  if (kind === 'A') { const t: { x: 1 } = [payload]; }
} }
const g5 = function ({ kind, payload } = cond ? { kind: 'A' as const, payload: null } : { kind: 'B' as const, payload: 's' }) {
  if (kind === 'A') { const t: { x: 1 } = [payload]; }
};
const sentinel: number = "";
