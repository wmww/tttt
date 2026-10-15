//// [test/cases/ttttSymbolConstructorSymbolProperty.ts] ////

//// [ttttSymbolConstructorSymbolProperty.ts]
// A readonly `symbol` property merged into the global SymbolConstructor is a unique symbol (tsc's compatibility special
// case for pre-3.0 libraries), but declaration emit writes the annotation as written, and a mutable one stays `symbol`
export const x = 1;
declare global {
    interface SymbolConstructor {
        readonly observable: symbol;
        mutable: symbol;
        method(): symbol;
    }
}
export const o = Symbol.observable;
export const m = Symbol.mutable;
export const obj = { [Symbol.observable]: 1 };


//// [ttttSymbolConstructorSymbolProperty.js]
// A readonly `symbol` property merged into the global SymbolConstructor is a unique symbol (tsc's compatibility special
// case for pre-3.0 libraries), but declaration emit writes the annotation as written, and a mutable one stays `symbol`
export const x = 1;
export const o = Symbol.observable;
export const m = Symbol.mutable;
export const obj = { [Symbol.observable]: 1 };


//// [ttttSymbolConstructorSymbolProperty.d.ts]
export declare const x = 1;
declare global {
    interface SymbolConstructor {
        readonly observable: symbol;
        mutable: symbol;
        method(): symbol;
    }
}
export declare const o: symbol;
export declare const m: symbol;
export declare const obj: {
    [Symbol.observable]: number;
};
