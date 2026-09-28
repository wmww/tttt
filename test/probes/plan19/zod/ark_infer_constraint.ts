// @strict: true
// `infer S extends C`: C is instantiated with what instantiated the conditional type before an inference that doesn't
// satisfy it falls back to it (arktype's `${infer submodule extends keyof $ & string}.${infer reference}` with $ = {}
// is never, so "string.trim" goes on to the ambient scope's branch). tsc: TS2322 '5' -> '0' at a, b and c, and
// '5' -> '"x"' at e
type Sub<token, $> = token extends `${infer s extends keyof $ & string}.${infer r}` ? [s, r] : 0;
type Resolve<token> = Sub<token, {}>;
export const a: Resolve<"string.trim"> = 5;
export const b: Sub<"string.trim", {}> = 5;
type G<T, C> = T extends [infer A extends C] ? A : 0;
export const c: G<["x"], number> = 5;
export const e: G<["x"], string> = 5;
// (tsc: 0, tttt: ["string", "trim"]: the conditional type resolves while X is still generic, and tttt doesn't fall
// back to a constraint that stays generic, see the HACK in ty_conditional_resolve())
type T3<X> = "string.trim" extends `${infer s extends keyof X & string}.${infer r}` ? [s, r] : 0;
export const d: T3<{}> = 5;
