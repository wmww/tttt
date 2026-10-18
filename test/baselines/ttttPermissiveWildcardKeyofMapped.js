//// [test/cases/ttttPermissiveWildcardKeyofMapped.ts] ////

//// [ttttPermissiveWildcardKeyofMapped.ts]
// The permissive instantiation (type parameters as the wildcard) decides whether a conditional type is definitely
// false: `keyof` the wildcard is the wildcard (tsc's getIndexType), and so is a homomorphic mapped type over it, even
// one whose type variable is constrained to arrays (tsc's instantiateConstituent returns the wildcard before mapping
// any to an array). Both stay deferred here; tttt resolved them to their false branch, and its eager ts-toolbelt
// `__ListOf` then recursed into TS5114 (ramdaToolsNoInfinite2)
type M<L extends any[]> = { [K in keyof L]?: L[K] };
declare function h1<T>(): { k: keyof T } extends { k: 'a' } ? 1 : 2;
export function w1<T>() { return h1<T>(); }
declare function h2<L extends any[]>(): { k: M<L> } extends { k: number } ? 1 : 2;
export function w2<L extends any[]>() { return h2<L>(); }
type Eq<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;
declare function h3<A extends object>(): Eq<Omit<A, 'a'>, {}> extends true ? void : { a: A };
export function w3<A extends object>() { return h3<A>(); }


//// [ttttPermissiveWildcardKeyofMapped.js]
export function w1() { return h1(); }
export function w2() { return h2(); }
export function w3() { return h3(); }


//// [ttttPermissiveWildcardKeyofMapped.d.ts]
type M<L extends any[]> = {
    [K in keyof L]?: L[K];
};
export declare function w1<T>(): {
    k: keyof T;
} extends {
    k: 'a';
} ? 1 : 2;
export declare function w2<L extends any[]>(): {
    k: M<L>;
} extends {
    k: number;
} ? 1 : 2;
type Eq<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;
export declare function w3<A extends object>(): Eq<Omit<A, "a">, {}> extends true ? void : {
    a: A;
};
export {};
