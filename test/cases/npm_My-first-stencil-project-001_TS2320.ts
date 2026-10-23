// origin: npm My-first-stencil-project-001@0.0.2 https://github.com/ionic-team/stencil-component-starter tsconfig.bench.json seed 20261029
// flip: yes
// An augmentation through a re-export (declare module 'outer' { interface Assertion ... }, outer re-exporting inner's
// Assertion) merges into a clone of the resolved symbol (tsgo's mergeSymbol: target not transient -> cloneSymbol(resolveSymbol)),
// so inner's interface itself never gets Base2: no TS2320 on it (@stencil/vitest's CustomMatchers on vitest's Assertion)
// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @filename: node_modules/inner/package.json
{"name": "inner", "types": "index.d.ts"}
// @filename: node_modules/inner/index.d.ts
interface Base1 { m(a: any): void }
interface Assertion<T = any> extends Base1 { t: T }
export type { Assertion };
// @filename: node_modules/outer/package.json
{"name": "outer", "types": "index.d.ts"}
// @filename: node_modules/outer/index.d.ts
import './aug';
export { Assertion } from 'inner';
// @filename: node_modules/outer/aug.d.ts
declare module 'inner' {
    interface Assertion<T> { u: T }
}
export {};
// @filename: a.ts
interface Base2 { m(a: string): number }
declare module 'outer' {
    interface Assertion extends Base2 {}
}
import type { Assertion } from 'outer';
declare const a: Assertion<number>;
export const r = a.m("x");
