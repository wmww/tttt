// @strict: true
// @noTypesAndSymbols: true
// An interface extending a generic function-type alias has the very same call signature as its base: the heritage
// check must not instantiate it with the interface's this-argument and relate it as a different generic signature,
// whose inference measures the variances of the aliases in its parameter (arktype's MatchParser extends
// CaseMatchParser<ctx.init<$>>: TS2589 + TS5114 in ark/docs; fixed in f973cab3). The aliases are in a .d.ts under
// skipLibCheck so that nothing else evaluates them (tsgo's .types walk of the .d.ts does, hence no types/symbols).
// @skipLibCheck: true
// @filename: parser.d.ts
type Ctx = { cases: unknown[]; $: unknown };
declare namespace ctx {
    type from<c extends Ctx> = c;
    type init<$> = from<{ cases: []; $: $ }>;
}
type Grow<T, N extends unknown[], ctx extends Ctx> =
    N["length"] extends 20 ? [T, ctx["$"]] : Grow<{ l: T; r: [T] }, [...N, 0], ctx> | Grow<[T], [...N, 1], ctx>;
type Validate<cases, ctx extends Ctx> = { v: Grow<cases, [], ctx> };
type CaseParser<ctx extends Ctx> = <const cases>(def: cases extends Validate<cases, ctx> ? cases : never) => cases;
// @filename: match.ts
export interface MatchParser<$> extends CaseParser<ctx.init<$>> {
    x: $;
}
