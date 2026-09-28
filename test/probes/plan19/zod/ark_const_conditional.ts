// @strict: true
// A `const` type parameter behind a conditional type (arktype's `type.validate<def, $>`) or a homomorphic mapped type
// still makes the argument's literals const (tsc's isConstTypeVariable: of a conditional type, its constraint). tsc
// infers `{ readonly name: "string"; }` for all three (TS2322 at x, y, z; TS2345 at the argument of h)
type V<def> = [def] extends [never] ? def : def extends string ? def : { [k in keyof def]: V<def[k]> };
declare function f<const def>(def: V<def>): def;
export const x: number = f({ name: "string" });
type W<def> = [def] extends [never] ? def : { [k in keyof def]: def[k] };
declare function g<const def>(def: W<def>): def;
export const y: number = g({ name: "string" });
type U<def> = [def] extends [never] ? def : 1;
declare function h<const def>(def: U<def>): def;
export const z: number = h({ name: "string" });
declare function m<const def>(def: { [k in keyof def]: def[k] }): def;
export const w: number = m({ name: "string" });
