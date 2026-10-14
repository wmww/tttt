// @noEmit: true
// A computed literal key is bound early by its value (tsc's getDeclarationName: `["kk"]` is `kk`); a computed entity
// name is bound late (lateBindMember), and its symbol merges into the early one of its name after all the early
// declarations (getResolvedMembersOrExportsOfSymbol's combineSymbolTables): the types, symbols, overload order and
// declaration checks all see the early declarations first

// Literal keys: one symbol with the written name
class Big {
    ["kk"]() {}
    kk() {}
}
abstract class Lit {
    ["lit"](): void;
    lit(x: number): void;
    lit(x?: number) {}
    #p(): void;
    #p() {}
    q(): void;
    ["q"]() {}
    [0x10](): void;
    16() {}
    #a = 1;
    ["#a"] = 2;
    get ["g"]() { return 1; }
    set g(v: number) {}
}
interface ILit {
    ["kk"]: number;
    kk: number;
}

// Late-bound methods merge into the early method: the early signatures, then the late ones
const k2 = "late";
class C2 {
    late(): void;
    [k2](): void;
    late() {}
}
new C2().late;
class C4 {
    [k2](x: string): void;
    late(x: number): void;
    late(x: any) {}
}
new C4().late;

// Late-bound properties
const k = "kk";
class C3 {
    [k]: number;
    kk: number;
}
interface I5 { [k]: number; kk: string; }
declare const i5: I5;
i5.kk;
interface I6 { [k]: number; }
interface I6 { kk: string; }
declare const i6: I6;
i6.kk;
type T = { [k]?: string; kk: string };

// Object literals
const o = { [k]: 1, kk: 2 };
o.kk;
const o2 = { ["kk"]: 1, kk: 2 };
const o3 = { [k]() {}, kk: 2 };
const o4 = { kk: 2, [k]: "s" };
o4.kk;

// Overload checks walk the merged symbol's declarations
const m = "m";
const s = Symbol();
abstract class Overloads {
    static m(): void;
    m() {}
    x: number;
    x(): void;
    x() {}
    abstract a(): void;
    b(): void;
    abstract a(x: number): void;
    [m](): void;
    [s](): void;
    [s](x: number): void;
    [s](x?: number) {}
    w(): void;
    [m]() {}
}
class S {
    static [m](): number;
    static m(x: string): string;
    static m(x?: any): any { return 1; }
}
S.m;

// A class checks its duplicate properties before late binding merged anything (tsc's getSymbolOfDeclaration of an
// early member before the first late-bound one is the early symbol alone): only a late-bound one first is TS2300
class V1 {
    kk: number;
    [k]: number;
}
class V2 {
    [k]: number;
    ["kk"]: number;
}
class V3 {
    ["kk"]: number;
    [k]: number;
}
class V5 {
    "kk": number;
    [k]: number;
}
class Twice {
    [k]: number;
    [k]: string;
}
// A late-bound property conflicting with an early method: reportMergeSymbolError, named as the late one is written
class MethodFirst {
    kk() {}
    [k]: number;
}
interface MethodFirstI {
    [k](): void;
    kk: number;
}
class SymbolKeys {
    [s]: number;
    [s]() {}
}
