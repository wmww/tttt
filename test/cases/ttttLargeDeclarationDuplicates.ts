// @strict: true
// @target: es2022
// @noEmit: true
// Declarations past the member counts where the checker indexes members by name (16: the class's namesake chains,
// the members builder, the duplicate tables, object literal builders): duplicates, overloads, accessor pairs, joined
// properties and computed keys behave as in small ones
const k = "kk";
declare const sym: unique symbol;

class Big {
    pad0 = 0;
    pad1 = 1;
    pad2 = 2;
    pad3 = 3;
    pad4 = 4;
    pad5 = 5;
    pad6 = 6;
    pad7 = 7;
    pad8 = 8;
    pad9 = 9;
    pad10 = 10;
    pad11 = 11;
    pad12 = 12;
    pad13 = 13;
    pad14 = 14;
    pad15 = 15;
    pad16 = 16;
    pad17 = 17;
    pad18 = 18;
    pad19 = 19;
    m(x: string): void;
    m(x: number): void;
    m(x: any) {}
    other() {}
    n(x: string): void;
    n2() {}
    n(x: number) {}
    dup() {}
    dup() {}
    static m(x: string): void;
    m2(x: string): void;
    static m2(x: number): void;
    m2(x: any) {}
    get acc() { return 1; }
    set acc(v: number) {}
    private get vis() { return 1; }
    public set vis(v: number) {}
    joined = 1;
    get joined2() { return 1; }
    prop = 1;
    prop = "s";
    meth() {}
    meth = 1;
    [sym]() {}
    [sym]() {}
    get [k + ""]() { return 1; }
    abstractish?(): void;
    abstractish?(x: number): void;
    late(): void;
    pad19b = 1;
    late() {}
}

interface BigI {
    pad0: number;
    pad1: number;
    pad2: number;
    pad3: number;
    pad4: number;
    pad5: number;
    pad6: number;
    pad7: number;
    pad8: number;
    pad9: number;
    pad10: number;
    pad11: number;
    pad12: number;
    pad13: number;
    pad14: number;
    pad15: number;
    pad16: number;
    pad17: number;
    pad18: number;
    pad19: number;
    a: string;
    a: number;
    b?: string;
    b: string;
    readonly c: string;
    c: string;
    m(): void;
    m: number;
    o(): void;
    o?(x: number): void;
    o: () => void;
}

const big = {
    pad0: 0,
    pad1: 1,
    pad2: 2,
    pad3: 3,
    pad4: 4,
    pad5: 5,
    pad6: 6,
    pad7: 7,
    pad8: 8,
    pad9: 9,
    pad10: 10,
    pad11: 11,
    pad12: 12,
    pad13: 13,
    pad14: 14,
    pad15: 15,
    pad16: 16,
    pad17: 17,
    pad18: 18,
    pad19: 19,
    a: 1,
    a: 2,
    get g() { return 1; },
    get g() { return 2; },
    s: 1,
    get s() { return 1; },
    m() {},
    m: 1,
    ...{ pad0: "x" },
    pad1: "y",
};
const n: number = big.pad0;
