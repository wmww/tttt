// An instantiation expression of an abstract class prints as its abstract construct signature alone (the class's
// `prototype` isn't written), a constructor type: parenthesized in an intersection or array like any function type
// (discord.js' builders' ts-mixer bases printed `abstract new () => Base<X> & typeof Mixin`)
// @strict: true
// @declaration: true
export abstract class Base<T> { t!: T }
export class Mix { m!: number }
declare function both<S1, S2>(a: S1, b: S2): S1 & S2;
export const intersected = both(Base<string>, Mix);
export const listed = [Base<number>];
export class Plain<T> { t!: T }
export const plain = both(Plain<string>, Mix);
