// Members in declaration order, the late-bound (symbol-keyed) ones among the others: tsgo's getNamedMembers sorts by
// compareSymbols (react-router's Headers 'prototype[Symbol.iterator]()', trpc's missing-properties list)
// @strict: true
// @target: esnext
// @filename: a.ts
interface It<T> { next(): T; [Symbol.dispose](): void }
interface I { a(): number; [Symbol.iterator](): It<number>; entries(): It<number>; b: string }
declare let x: I;
declare const y: { a(): number; [Symbol.iterator](): { next(): number }; entries(): { next(): number }; b: string };
x = y;
interface J { a: 1 }
interface J { [Symbol.iterator]: 1; b: 1 }
class C { [Symbol.dispose]() {} x = 1; [Symbol.iterator]() {} y = 2; static [Symbol.dispose] = 1; static z = 1; static [Symbol.iterator] = 1; }
const c: { q: 1 } = new C();
const j: J = {};
const d: typeof C = { prototype: null! };
