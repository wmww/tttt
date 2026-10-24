//// [test/cases/ttttMergedClassInterfaceIsClass.ts] ////

//// [ttttMergedClassInterfaceIsClass.ts]
// A class merged with an interface is a class to tsc wherever it asks ObjectFlags.Class (getDeclaredTypeOfClassOrInterface
// gives the merged symbol a class type): the static member suggestion (TS2576), a `#x` of another class (TS2322 with its
// own message), subtype reduction (two unrelated classes stay), constructor narrowing
interface Iface { a: number }
interface Extra { e: string }
declare class Merged { m: number; static s: number; constructor(); }
interface Merged extends Extra {}
declare class Plain { m: number; static s: number; constructor(); }
declare const u1: Iface | Merged;
declare const u3: Plain | Iface;
export const t1: number = u1;
export const t3: number = u3;
declare const mi: Merged;
mi.s;
declare const pi: Plain;
pi.s;
declare const x: Merged | Iface;
if (x.constructor === Merged) { const n: number = x; }
class Priv { #p = 1; q = 2; }
interface Priv { r: number }
class Priv2 { #p = 1; q = 2; r = 3 }
export const pp: Priv2 = new Priv();
declare class Base { on(e: string): this; x: number }
declare class BC extends Base { constructor(); }
interface BC extends Base { close(): void }
declare class DF extends Base { constructor(); }
interface DF extends Base { close(): void; params(l: number): void }
declare const c: boolean;
declare const d: DF;
declare const b: BC;
export const both = c ? d : b;
export class Gen { field = 1; method<T>(x: T): T { return x; } }
export interface Gen { extra: string }
export const g = new Gen();
export const gm = g.method;


//// [ttttMergedClassInterfaceIsClass.js]
export const t1 = u1;
export const t3 = u3;
mi.s;
pi.s;
if (x.constructor === Merged) {
    const n = x;
}
class Priv {
    #p = 1;
    q = 2;
}
class Priv2 {
    #p = 1;
    q = 2;
    r = 3;
}
export const pp = new Priv();
export const both = c ? d : b;
export class Gen {
    field = 1;
    method(x) { return x; }
}
export const g = new Gen();
export const gm = g.method;


//// [ttttMergedClassInterfaceIsClass.d.ts]
export declare const t1: number;
export declare const t3: number;
declare class Priv2 {
    #private;
    q: number;
    r: number;
}
export declare const pp: Priv2;
declare class Base {
    on(e: string): this;
    x: number;
}
declare class BC extends Base {
    constructor();
}
interface BC extends Base {
    close(): void;
}
declare class DF extends Base {
    constructor();
}
interface DF extends Base {
    close(): void;
    params(l: number): void;
}
export declare const both: BC | DF;
export declare class Gen {
    field: number;
    method<T>(x: T): T;
}
export interface Gen {
    extra: string;
}
export declare const g: Gen;
export declare const gm: <T>(x: T) => T;
export {};
