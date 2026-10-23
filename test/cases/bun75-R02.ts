// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { make, make2, make3, cls } from "./m";
export const v = make();
export const v2 = make2();
export const v3 = make3();
export const v4 = cls();
export function f(p = make()) { return [make2()]; }
export class C { p = make(); static s = make2(); m() { return make3(); } }
const sentinel: number = "";
// @filename: m.ts
namespace N { export default class Foo { x = 1 } }
namespace N2 { export default class { y = 1 } }
namespace N3 { export default interface Ifc { z: number } }
export function make() { return new N.default(); }
export function make2() { return new N2.default(); }
export function make3() { return null! as N3.default; }
export function cls() { return N.default; }
