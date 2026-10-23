// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function pair<A extends unknown[], R, B extends unknown[], S>(f: new (...a: A) => R, g: new (...b: B) => S): (a: A, b: B) => [R, S];
declare function pairF<A extends unknown[], R, B extends unknown[], S>(f: (...a: A) => R, g: (...b: B) => S): (a: A, b: B) => [R, S];
declare function f1<T>(t: T): T;
declare function f2<T, U extends T[]>(t: T, u: U): [T, U];
declare function f3<U extends T[], T>(t: T, u: U): [T, U];
declare function f4<T, U = T>(t: T, u: U): [T, U];
declare function f5<T, K extends keyof T>(t: T, k: K): T[K];
declare class C1<T> { constructor(t: T) }
declare class C2<T, U extends T[]> { constructor(t: T, u: U) }
declare class C3<U extends T[], T> { constructor(t: T, u: U) }
declare class C4<T, U = T> { constructor(t: T, u: U) }
declare class C5<T, K extends keyof T> { constructor(t: T, k: K) }
const a1: 0 = pairF(f1, f2);
const a2: 0 = pairF(f1, f3);
const a3: 0 = pairF(f1, f4);
const a4: 0 = pairF(f1, f5);
const a5: 0 = pairF(f2, f1);
const b1: 0 = pair(C1, C2);
const b2: 0 = pair(C1, C3);
const b3: 0 = pair(C1, C4);
const b4: 0 = pair(C1, C5);
const b5: 0 = pair(C2, C1);
const sentinel: number = "";
