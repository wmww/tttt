// @declaration: true
// An unused import of a module the file also `export *`s is elided: tsc's isImportRequiredByAugmentation only keeps
// `import "m"` for an export whose merged symbol has a declaration in m (an augmentation), not for one `export *`
// merely re-exports (vue's index: `import * as runtimeDom from '@vue/runtime-dom'` and `export * from` it). And only
// named imports are kept for an augmentation (transformImportDeclaration): not a namespace import
// @filename: b.ts
export interface Thing { a: number }
export const thing = 1;
// @filename: c.ts
export interface Other { o: number }
// @filename: index.ts
import * as b from './b';
import type { Thing } from './b';
export * from './b';
const x = b.thing;
export const y = 2;
// @filename: aug.ts
import { Other } from './c';
export { Other };
declare module './c' {
    interface Other { extra: string }
}
// @filename: aug2.ts
import * as c from './c';
export * from './c';
declare module './c' {
    interface Other { more: string }
}
// @filename: aug3.ts
import * as d from './d';
import type { D } from './d';
export interface X { a: number }
// @filename: d.ts
import {} from './aug3';
declare module './aug3' {
    interface X { b: number }
}
export interface D {}
// @filename: e.ts
export interface E { e: number }
// @filename: f.ts
export {};
declare module './e' {
    interface E { f: number }
}
// @filename: index2.ts
import type { E } from './e';
export * from './e';
export const z = 1;
