// @strict: true
// @target: es2022
type LiteralValue = string | number | boolean | null | bigint
declare function Literal<Literals extends readonly [LiteralValue, ...LiteralValue[]]>(...literals: Literals): Literals
export const a = Literal("Left")
declare function L2<Literals extends readonly LiteralValue[]>(...literals: Literals): Literals
export const b = L2("Left", 1)
