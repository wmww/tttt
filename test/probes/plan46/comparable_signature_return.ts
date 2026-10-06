// The comparable relation relates signature return types source to target only (tsc's compareSignaturesRelated: both
// ways only for a bivariant callback): vscode extHostTestItem.ts 191/192 under flagflip's non-strictNullChecks cells
// @strictNullChecks: false,true
interface A { a: string }
interface B extends A { b: string }
declare const g: { get(): A; x: 1 }
export const r8 = g as { get(): B }
declare const g2: { v: A; x: 1 }
export const r9 = g2 as { v: B }
