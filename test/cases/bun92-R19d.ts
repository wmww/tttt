// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"module":"preserve","moduleResolution":"bundler","allowJs":true,"checkJs":true } }
// @filename: a.ts
import * as j1 from "./j1";
import { K, o as jo } from "./j3";
type S<X> = [Extract<keyof X, number>, Extract<keyof X, string>];
const t1: S<typeof j1> = ["?", -99], t2: S<K> = ["?", -99], t3: S<typeof K> = ["?", -99], t4: S<typeof jo> = ["?", -99];
const sentinel: number = "";
// @filename: j1.js
exports[0] = 1; exports["1"] = 2; exports.a = 3;
// @filename: j3.js
export class K { constructor() { this[0] = 1; this["1"] = 2; this.a = 3; } }
K[5] = 1; K["6"] = 2;
export const o = {}; o[0] = 1; o["1"] = 2; o.a = 3;
Object.defineProperty(o, "3", { value: 1 }); Object.defineProperty(o, 4, { value: 1 });
