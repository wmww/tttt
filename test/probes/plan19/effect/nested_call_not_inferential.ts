// @strict: true
type LiteralValue = string | number | boolean | null | bigint
declare function isSymbol(input: unknown): input is symbol
declare function Sym(s: symbol): 1
declare function Lit2<L extends LiteralValue>(l: L): L
declare function Struct<F>(fields: F): F
export const s = <V extends LiteralValue | symbol>(value: V) => Struct({ a: isSymbol(value) ? Sym(value) : Lit2(value) })
export const s2 = <V extends LiteralValue | symbol>(value: V) => Struct(isSymbol(value) ? 1 : Lit2(value))
