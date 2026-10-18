//// [test/cases/ttttAliasBodyMappedTypeIdentity.ts] ////

//// [ttttAliasBodyMappedTypeIdentity.ts]
// `type N = Readonly<X>`: the instance tsc makes for the alias is cached under N but keeps Readonly's name, so it is a
// type of its own next to `Readonly<X>` written elsewhere (and next to N2's)
interface Foo { a: string }
type N = Readonly<Foo>;
type N2 = Readonly<Foo>;
declare let n: N | Readonly<Foo> | N2;
export const u = n;
export const v = [n, null as any as Readonly<Foo>] as const;
type G<T> = Readonly<{ a: T }>;
declare let g: G<number> | Readonly<{ a: number }>;
export const w = g;


//// [ttttAliasBodyMappedTypeIdentity.js]
export const u = n;
export const v = [n, null];
export const w = g;


//// [ttttAliasBodyMappedTypeIdentity.d.ts]
interface Foo {
    a: string;
}
export declare const u: Readonly<Foo> | Readonly<Foo> | Readonly<Foo>;
export declare const v: readonly [Readonly<Foo> | Readonly<Foo> | Readonly<Foo>, Readonly<Foo>];
export declare const w: Readonly<{
    a: number;
}> | Readonly<{
    a: number;
}>;
export {};
