//// [test/cases/ttttStaticUniqueSymbolField.ts] ////

//// [ttttStaticUniqueSymbolField.ts]
// A static readonly field initialized with Symbol() or Symbol.for() declares a unique symbol owned by the class:
// declaration emit writes `unique symbol` for the field itself (tsc's AllowUniqueESSymbolType), not `typeof S.X`
export class S {
    static readonly PATH_SYMBOL = Symbol.for('x');
    static readonly P2 = Symbol();
    readonly p3 = Symbol();
    static readonly copy = S.P2;
}


//// [ttttStaticUniqueSymbolField.js]
// A static readonly field initialized with Symbol() or Symbol.for() declares a unique symbol owned by the class:
// declaration emit writes `unique symbol` for the field itself (tsc's AllowUniqueESSymbolType), not `typeof S.X`
export class S {
    constructor() {
        this.p3 = Symbol();
    }
}
S.PATH_SYMBOL = Symbol.for('x');
S.P2 = Symbol();
S.copy = S.P2;


//// [ttttStaticUniqueSymbolField.d.ts]
export declare class S {
    static readonly PATH_SYMBOL: unique symbol;
    static readonly P2: unique symbol;
    readonly p3: symbol;
    static readonly copy: symbol;
}
