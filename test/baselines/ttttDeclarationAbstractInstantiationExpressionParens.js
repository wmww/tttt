//// [test/cases/ttttDeclarationAbstractInstantiationExpressionParens.ts] ////

//// [ttttDeclarationAbstractInstantiationExpressionParens.ts]
// An instantiation expression of an abstract class prints as its abstract construct signature alone (the class's
// `prototype` isn't written), a constructor type: parenthesized in an intersection or array like any function type
// (discord.js' builders' ts-mixer bases printed `abstract new () => Base<X> & typeof Mixin`)
export abstract class Base<T> { t!: T }
export class Mix { m!: number }
declare function both<S1, S2>(a: S1, b: S2): S1 & S2;
export const intersected = both(Base<string>, Mix);
export const listed = [Base<number>];
export class Plain<T> { t!: T }
export const plain = both(Plain<string>, Mix);


//// [ttttDeclarationAbstractInstantiationExpressionParens.js]
// An instantiation expression of an abstract class prints as its abstract construct signature alone (the class's
// `prototype` isn't written), a constructor type: parenthesized in an intersection or array like any function type
// (discord.js' builders' ts-mixer bases printed `abstract new () => Base<X> & typeof Mixin`)
export class Base {
    t;
}
export class Mix {
    m;
}
export const intersected = both(Base, Mix);
export const listed = [Base];
export class Plain {
    t;
}
export const plain = both(Plain, Mix);


//// [ttttDeclarationAbstractInstantiationExpressionParens.d.ts]
export declare abstract class Base<T> {
    t: T;
}
export declare class Mix {
    m: number;
}
export declare const intersected: (abstract new () => Base<string>) & typeof Mix;
export declare const listed: (abstract new () => Base<number>)[];
export declare class Plain<T> {
    t: T;
}
export declare const plain: {
    new (): Plain<string>;
} & typeof Mix;
