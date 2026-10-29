// @strict: true
// A rest parameter typed by a union of tuples gives each argument its contextual type without reductions
// (getTypeOfPropertyOfContextualType): `string | unknown` at 0 may be primitive, so the literal 0 stays a literal in the
// spread tuple the error prints
class EventEmitter<ET> {
    off<K extends keyof ET>(...args: [K, number] | [unknown, string]): void {}
}
function once<ET, T extends EventEmitter<ET>>(emittingObject: T, eventName: keyof ET): void {
    emittingObject.off(0, eventName);
}
declare function f<K extends string>(...args: [K, number] | [unknown, string]): void;
declare const s: string | number | symbol;
f(0, s);
declare function g(...args: [string, number] | [unknown, string]): void;
g(0, s);
