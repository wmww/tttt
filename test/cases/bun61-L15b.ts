// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
interface I { default class: 1 } declare const i: I; i.class; i.default;
type T = { default function: 1 }; declare const t: T; t.function; t.default;
class C { default class = 1 } declare const c: C; c.class; c.default;
interface L { default class: 1; default: 2 }
const sentinel: number = "";
