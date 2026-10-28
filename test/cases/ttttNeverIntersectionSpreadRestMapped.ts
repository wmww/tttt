// @strict: true
// An intersection that reduces to never (a private and a public `id`) spreads and rests like never (TS2698, TS2700:
// tsc's checkObjectLiteral and getBindingElementTypeFromParentType reduce first) and a mapped type over its keys has
// no properties (getPropertiesOfType of the reduced apparent type)
declare class D { private readonly id: string | number; other: 1 }
declare const wdp: D & { id: string | number }
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
const wdp_spread = { ...wdp }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
declare const u: { a: 1 } | (D & { id: string })
const u_spread = { ...u }
const { a: u_a, ...u_rest } = u
