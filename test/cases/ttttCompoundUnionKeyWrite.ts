// @strict: true
// @noUncheckedIndexedAccess: true
// A compound assignment to an element access with a union of keys is written: the intersection of the keys' types
declare const k: "a" | "b";
declare const s: { a: number; b?: number };
s[k] += 1;
s[k]++;
declare const both: { a?: number; b?: number };
both[k] += 1;
declare const dict: { [x: string]: number };
dict[k] += 1;
declare const mixed: { a: number; [x: string]: number };
mixed[k] += 1;
mixed[k]--;
declare const strs: { a: string; b?: string };
strs[k] += "x";
const r1 = s[k] += 1;
declare const kk: "a" | "b" | "c";
declare const s3: { a: number; b: string; c?: number };
s3[kk] += 1;
