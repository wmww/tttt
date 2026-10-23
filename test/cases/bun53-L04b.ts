// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { "0x10n": number; "16n": string; "0xffn": 1; "255n": 2 };
const { 0x10n: a } = o; const a1: never = a;
const { 0XFFn: c } = o; const c1: never = c;
const o1 = { 0b11n: 1, 3n: 2 };
const o2 = { 0XFFn: 1, 0xffn: 2 };
const o4 = { 0o7n: 1, "7n": 2 };
const o6 = { 0b11n: 1, "0b11n": 2 };
declare function dec(t: any, c: { name: "0xffn" }): void;
declare function dec2(t: any, c: { name: "3n" }): void;
class C { @dec 0XFFn() {} @dec2 0b11n() {} }
const sentinel: number = "";
