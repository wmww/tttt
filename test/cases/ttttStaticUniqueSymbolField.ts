// @declaration: true
// @strict: true
// @target: es2015
// A static readonly field initialized with Symbol() or Symbol.for() declares a unique symbol owned by the class:
// declaration emit writes `unique symbol` for the field itself (tsc's AllowUniqueESSymbolType), not `typeof S.X`
export class S {
    static readonly PATH_SYMBOL = Symbol.for('x');
    static readonly P2 = Symbol();
    readonly p3 = Symbol();
    static readonly copy = S.P2;
}
