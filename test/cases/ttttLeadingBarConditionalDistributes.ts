// @strict: true
// A conditional type's check type after a leading `|` or `&` is a union or intersection node of one type: the type
// parameter itself, so the conditional distributes (tsc's isDistributive looks at the type, not the node)
type F<X> = | X extends string ? 1 : 2
type T1 = F<string | number>
declare const t: T1; const u: 3 = t;
type H<X> = | X extends string ? X : 2
function g<Y extends string>(y: H<Y>) { const q: 3 = y; }
type G<X> = & X extends string ? 1 : 2
type T2 = G<string | number>
declare const t2: T2; const u2: 3 = t2;
