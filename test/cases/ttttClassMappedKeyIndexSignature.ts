// @strict: true
// @noUnusedLocals: true
// `[K in T]` as a class member's name is TS7061 at the first member, an index signature too; with an index signature
// of its staticness tsc checks the key expression after all (checkIndexConstraints' getTypeOfExpression), errors
// and uses included, else quietly (bun's 88-R05b crashed on `this` and `super` in such a key)
export {};
function f1() {
    const kk = "x"; const oo = {};
    class D1 { [k: string]: any; [kk in oo]: number = 1 }
    return D1;
}
function f2() {
    const kk = "x"; const oo = {};
    class D2 { [kk in oo]: number = 1 }
    return D2;
}
f1; f2;
export class D4 { [k: string]: any; [missing1.a.b in missing2()]: number = 1 }
export class D6 { [k: string]: any; [this.x in super.y]: number = 1 }
export class D7 { static [k: string]: any; [missing3 in missing4]: number = 1 }
