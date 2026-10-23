// An optional annotation is reprinted when it is the type without undefined by type facts (tsc's
// typeNodeIsEquivalentToType: getTypeWithFacts(NEUndefined)): not `x?: T` with T constrained to undefined, nor `x?: void`
function g<T>(a: { x?: T }) { return a; }
function h(a: { x?: number }) { return a; }
function k(a: { x?: void }) { return a; }
function m<T extends number>(a: { x?: T }) { return a; }
declare const v: { x?: unknown; y?: any };
function n<T extends string | undefined>(a: { x?: T }) { return a; }
