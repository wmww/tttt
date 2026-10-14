// @strict: true
// A type parameter against a union also relates as its constraint hoisted into an intersection with it (tsc's
// structuredTypeRelatedTo, getEffectiveConstraintOfIntersection): `Value extends A | L1 | L2` to `(A & Value) | (L1 & Value) |
// (L2 & Value)`, where L1 and L2 share a discriminant (svelte's builders.js prop(), ESTree's literals)
interface A { type: "A"; a: 1 } interface L1 { type: "L"; value: string } interface L2 { type: "L"; value: bigint; bigint: string }
interface P1 { type: "P" }
type Expression = A | L1 | L2;
interface Property { type: "Property"; value: Expression | P1; key: string }
function prop<Value extends Expression>(key: string, value: Value): Property & { value: Value } {
    return { type: "Property", key, value };
}

interface Shape { name: string; width: number; }
declare function f<K extends keyof Shape>(k: K): void;
function g<T extends string | number>(t: T) { f(t); let k: keyof Shape = t; }
