// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function gen<T>(a: T[]): T;
declare function over(a: number[], b: string): 1; declare function over(a: number[], b: number): 2;
declare function wrap<T>(a: T): T;
declare function call<T>(a: () => T): T;
declare const b1: { [Symbol.iterator](): { next(): IteratorResult<number>; return: number } };
gen([...b1]);
declare const b2: { [Symbol.iterator](): { next(): IteratorResult<number>; return: string } };
over([...b2], 1);
declare const b3: { [Symbol.iterator](): { next(): IteratorResult<number>; return: boolean } };
[1].concat([...b3]);
declare const b4: { [Symbol.iterator](): { next(): { done: boolean } } };
gen([...b4]);
declare const b5: { [Symbol.iterator](): { next(): IteratorResult<number>; throw: number } };
wrap({ a: [...b5] });
declare const b6: { [Symbol.iterator](): { next(): IteratorResult<number>; throw: string } };
call(() => [...b6]);
// one signature that is not generic: both tools report
declare function one(a: number[]): void;
declare const b7: { [Symbol.iterator](): { next(): IteratorResult<number>; throw: boolean } };
one([...b7]);
const sentinel: number = "";
