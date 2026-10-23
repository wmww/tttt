// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "target": "es2022" } }
// @filename: a.ts
declare function mdec<A extends any[], R>(m: (...a: A) => R, ctx: ClassMethodDecoratorContext): (...a: A) => R;
class D { @mdec m<T>(x: T): T[] { return [x]; } }
declare function cdec<A extends any[], R>(c: new (...a: A) => R, ctx: ClassDecoratorContext): new (...a: A) => R;
@cdec class C<T> { constructor(public x: T) {} }
const sentinel: number = "";
