//// [test/cases/ttttDeclarationExpandoNameValue.ts] ////

//// [ttttDeclarationExpandoNameValue.ts]
// An expando member whose value is a name is `export { v as x }` in the namespace: the local it names is kept
const _skip = Symbol();
const DEFAULT_TIMEOUT = 60 * 1000;
type KeysMap = { a: number };
const keys: KeysMap = { a: 1 };
export function f() {}
f.skip = _skip;
f.DEFAULT_TIMEOUT = DEFAULT_TIMEOUT;
f.keys = keys;


//// [ttttDeclarationExpandoNameValue.js]
// An expando member whose value is a name is `export { v as x }` in the namespace: the local it names is kept
const _skip = Symbol();
const DEFAULT_TIMEOUT = 60 * 1000;
const keys = { a: 1 };
export function f() { }
f.skip = _skip;
f.DEFAULT_TIMEOUT = DEFAULT_TIMEOUT;
f.keys = keys;


//// [ttttDeclarationExpandoNameValue.d.ts]
declare const _skip: unique symbol;
declare const DEFAULT_TIMEOUT: number;
type KeysMap = {
    a: number;
};
declare const keys: KeysMap;
export declare function f(): void;
export declare namespace f {
    export { _skip as skip };
    export { DEFAULT_TIMEOUT };
    export { keys };
}
export {};
