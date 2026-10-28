// @strict: true
// A base constructor's return type is reduced first: `D & (J | W)` with D & J never (a private and a public id) is the
// base D & W, so the derived class's public id is TS2415 (tsc's resolveBaseTypesOfClass, getReducedType)
interface J { id: string | number }
declare class D { private readonly id: string | number; other: 1 }
type W = Record<'id', string | number>
declare const dju: D & (J | W)
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const x: D & J
const y: never = x
