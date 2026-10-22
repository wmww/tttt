// A property related to an `unknown` (or any) target property is related without asking for the source property's
// type (tsc's isPropertySymbolTypeRelated): asking resolves `I.p` here, from inside `v`'s initializer, a cycle tsc
// never enters (TS7022 on v, TS2502 on p). better-drizzle's TS2502 at plugins.ts:742 at some checker counts
const v = i satisfies { p: unknown };
interface I { p: typeof v }
declare const i: I;
const s: string = v.p;
const t: { p: any } = i;
