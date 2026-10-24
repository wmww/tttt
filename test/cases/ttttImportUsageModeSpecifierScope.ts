// An import's or export's usage mode (ESM here: module esnext, bundler) picks `.js` endings for the modules printed
// in its names' types (tsc's originalModuleSpecifier), but not inside a signature with parameters or type parameters,
// whose fake scope is the enclosing declaration then (tsc's enterNewScope); and the containers tie on path components
// (`./types/index.js` and `./types/shared.js`), so the first in order is taken
// @module: esnext
// @moduleResolution: bundler
// @filename: src/types/shared.ts
export type MF = { a: 1 };
// @filename: src/types/index.ts
export * from './shared';
// @filename: src/cm.ts
import { MF } from './types';
export declare function cm<T extends MF>(x: T): T;
export declare function cm0<T extends MF>(): T;
export declare const v: MF;
export declare const g: { f(x: number): MF; h(): MF };
// @filename: src/index.ts
export { cm, cm0, v, g } from './cm';
// @filename: demo/index.ts
import { cm, cm0, v, g } from '../src';
cm({ a: 1 });
