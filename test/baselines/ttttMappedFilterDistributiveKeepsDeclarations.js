//// [test/cases/ttttMappedFilterDistributiveKeepsDeclarations.ts] ////

//// [ttttMappedFilterDistributiveKeepsDeclarations.ts]
// A mapped type whose `as` clause is a distributive conditional filtering its keys (`k extends X ? never : k`) only
// filters (tsc's getMappedTypeNameTypeKind: the clause is assignable to the key, its branch being the key's
// distributed twin), so its properties are declared where the source's are and keep their order (compareSymbols by
// declaration), not sorted by name (arktype's `type({ name, age })`)
type show<t> = { [k in keyof t]: t[k] } & unknown;
declare function f1<const def>(d: def): show<{ [k in keyof def as k extends `${infer n}?` ? never : k]: def[k] }>;
declare function f2<const def>(d: def): { [k in keyof def as k extends `${infer n}?` ? never : k]: def[k] };
declare function f3<const def>(d: def): { [k in keyof def as k extends "zz" ? never : k]: def[k] };
export const a1 = f1({ name: "s", age: 1 });
export const a2 = f2({ name: "s", age: 1 });
export const a3 = f3({ name: "s", age: 1, zz: 2 });


//// [ttttMappedFilterDistributiveKeepsDeclarations.js]
export const a1 = f1({ name: "s", age: 1 });
export const a2 = f2({ name: "s", age: 1 });
export const a3 = f3({ name: "s", age: 1, zz: 2 });


//// [ttttMappedFilterDistributiveKeepsDeclarations.d.ts]
export declare const a1: {
    readonly name: "s";
    readonly age: 1;
};
export declare const a2: {
    readonly name: "s";
    readonly age: 1;
};
export declare const a3: {
    readonly name: "s";
    readonly age: 1;
};
