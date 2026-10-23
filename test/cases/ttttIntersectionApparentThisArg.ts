// An intersection's members see it as their `this` when its type variables are made apparent (tsc's
// getApparentTypeOfIntersectionType: getTypeWithThisArgument with the original intersection)
interface I { me: this; f(): this }
function g<T>(t: T & I) { const a = t.me; const b = t.f(); return [a, b] }
function h(t: { x: 1 } & I) { const a = t.me; const b = t.f(); return [a, b] }
function k<T extends { y: 2 }>(t: T & I) { const a = t.me; return a }
declare function f<U>(x: { me: U }): U;
function g2<T>(t: T & I): T & I { return f(t); }
function g3<T>(t: T & I): T & I { return t.me; }
