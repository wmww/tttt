// @strict: true
// @target: es2022
// @lib: es2022,esnext.decorators
declare function mdec<A extends any[], R>(m: (...a: A) => R, ctx: ClassMethodDecoratorContext): (...a: A) => R;
class D { @mdec m<T>(x: T): T[] { return [x]; } }
declare function cdec<A extends any[], R>(c: new (...a: A) => R, ctx: ClassDecoratorContext): new (...a: A) => R;
@cdec class C<T> { constructor(public x: T) {} }
declare function plain<A extends any[], R>(m: (...a: A) => R, ctx: number): (...a: A) => R;
declare function g<T>(x: T): T[];
const r = plain(g, 1);
