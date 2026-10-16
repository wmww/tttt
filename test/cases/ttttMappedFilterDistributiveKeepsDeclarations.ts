// @declaration: true
// @strict: true
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
// What the shape doesn't tell is related: arktype's keys through `infer` (also a key renamed by the clause, `email?`
// to `email`, keeps its declaration: tsc links every property of a filtering mapped type to its key's)
type preparse<k> = k extends `${infer inner}?` ? { kind: "optional"; normalized: inner } : { kind: "required"; normalized: k };
type reqKey<k> = preparse<k> extends infer parsed ? parsed extends { kind: "required"; normalized: infer n } ? n : never : never;
type optKey<k> = preparse<k> extends infer parsed ? parsed extends { kind: "optional"; normalized: infer n } ? n : never : never;
type infer_<def> = show<{ -readonly [k in keyof def as reqKey<k>]: def[k] } & { -readonly [k in keyof def as optKey<k>]?: def[k] }>;
declare function type<const def>(d: def): infer_<def>;
export const User = type({ name: "string", age: "number", "email?": "string", tags: "string[]", role: "x" });
