// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
export {};
type Action = { kind: 'A', payload: number } | { kind: 'B', payload: string };
function f1({ kind, payload }?: Action) {
  if (kind === 'A') { const t: null = payload; }
}
function f2([kind, payload]?: ['A', number] | ['B', string]) {
  if (kind === 'A') { const t: null = payload; }
}
const f3 = function ({ kind, payload }?: Action) {
  if (kind === 'A') { const t: null = payload; }
};
class C { m({ kind, payload }?: Action) { if (kind === 'A') { const t: null = payload; } } }
function f4({ kind, payload }?: Action | undefined) {
  if (kind === 'A') { const t: null = payload; }
}
function f5({ kind, payload }: Action | undefined) {
  if (kind === 'A') { const t: null = payload; }
}
const sentinel: number = "";
// @filename: b.js
export {};
/** @typedef {{ kind: 'A', payload: number } | { kind: 'B', payload: string }} Action */
/** @param {Action} [p] */
function f1({ kind, payload }) {
  if (kind === 'A') { /** @type {null} */ const t = payload; }
}
/** @param {Action=} p */
function f2({ kind, payload }) {
  if (kind === 'A') { /** @type {null} */ const t = payload; }
}
/** @type {number} */
const sentinel = "";
