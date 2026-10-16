//// [test/cases/ttttContravariantInferenceAliasedIntersection.ts] ////

//// [ttttContravariantInferenceAliasedIntersection.ts]
// A type parameter inferred from one contravariant candidate under a combining priority (return type inference: a
// generic function argument contextually typed by `(v: T) => R`) is the intersection of the candidates, tsc's
// getIntersectionType, which flattens a lone aliased intersection into its members: the inferred `C` is
// `Base & { deps: string[] }`, not `Ctx` (feathers' generators printed `Promise<AppGeneratorContext>`)
export type Base = { cwd: string };
export type Ctx = Base & { deps: string[] };
declare const render: <C extends Base>(t: string) => (ctx: C) => C;
declare function call<T, R>(t: T, f: (v: T) => R): R;
declare const ctx: Ctx;
export const viaCall = call(ctx, render('x'));
export const viaArrow = (c: Ctx) => call(c, render('x'));
declare const id: <C extends Base>(c: C) => C;
export const direct = id(ctx);


//// [ttttContravariantInferenceAliasedIntersection.js]
export const viaCall = call(ctx, render('x'));
export const viaArrow = (c) => call(c, render('x'));
export const direct = id(ctx);


//// [ttttContravariantInferenceAliasedIntersection.d.ts]
export type Base = {
    cwd: string;
};
export type Ctx = Base & {
    deps: string[];
};
export declare const viaCall: Base & {
    deps: string[];
};
export declare const viaArrow: (c: Ctx) => Base & {
    deps: string[];
};
export declare const direct: Ctx;
