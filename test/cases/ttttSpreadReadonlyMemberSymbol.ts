// @target: es2015
// @noEmit: true
// @filename: /node_modules/foo/index.d.ts
// A readonly member spread (into an object literal, or the synthetic namespace of `import * as` of a callable
// `export =`) is a fresh symbol without a parent: a bare name in the symbols baseline
declare function foo(): void;
declare namespace foo { export const items: string[]; export let other: number; }
export = foo;
// @filename: /index.ts
import * as foo from 'foo';
foo.items;
foo.other;
namespace ns { export const x = 1; export let y = 2; export function f() {} }
const o = { ...ns };
o.x; o.y; o.f;
const o2 = { ...o };
o2.x; o2.y;
