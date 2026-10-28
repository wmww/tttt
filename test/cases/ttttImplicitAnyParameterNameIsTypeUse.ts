// @noUnusedLocals: true
// @noUnusedParameters: true
// @noImplicitAny: false
// @filename: b.ts
export interface Foo { } export interface Bar { } export interface Baz { } export interface Qux { }
// @filename: a.ts
// An untyped parameter of a function type, call or method signature whose name is a type (tsc's reportImplicitAny,
// TS7051 under noImplicitAny) resolves that name as a use either way, the signature's own type parameters first; a
// construct signature's doesn't
import { Foo } from "./b"; import { Bar } from "./b"; import { Baz } from "./b"; import { Qux } from "./b";
export type F1 = (Foo) => void;
export interface F2 { m(Bar): void; (Baz): void; new (Qux): void }
export type F4 = <T>(T) => void;
export type F9 = (Local) => void; type Local = 1;
// @filename: c.ts
export type G1 = <T>(T) => void;
export interface G2 { m<U>(U): void }
