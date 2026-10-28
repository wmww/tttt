// @target: es2020
// @noEmit: true
const o7 = { 1n: 1, "1n": 3 };
const o6 = { 0o7n: 1, "7n": 2 };
const o8 = { 0x1Fn: 1 };
const o9 = { 10n: 1 };
o9["10n"];
declare const o: { "0x10n": number; "16n": string; "0xffn": 1; "255n": 2 };
const { 0x10n: a } = o; const a1: never = a;
const { 0XFFn: c } = o; const c1: never = c;
const { 0b11n: d } = { "3n": true };
const p1 = { 0b11n: 1, 3n: 2 };
const p2 = { 0XFFn: 1, 0xffn: 2 };
const p4 = { 0o7n: 1, "7n": 2 };
const p6 = { 0b11n: 1, "0b11n": 2 };
declare function dec(t: any, c: { name: "0xffn" }): void;
declare function dec2(t: any, c: { name: "3n" }): void;
class C2 { @dec 0XFFn() {} @dec2 0b11n() {} }
class K { 1n() {} "1n"() {} 0x1Fn = 1; 0x1fn = 2 }
interface I { 1n: number; "1n": string }
