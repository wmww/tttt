// @strict: true
// A shorthand assignment pattern's default (`({ b = d } = o)`) has no contextual type (tsc's getContextualType of a
// shorthand's initializer is its object literal's, none as an assignment target), and the property loses undefined
// unless the default's type facts say IsUndefined: any and unknown don't, a type variable by its constraint (tsc's
// checkDestructuringAssignment)
namespace N1 {
declare const o: { a?: number; b?: string };
let b: string;
({ b = 5 as any } = o);
({ b = (5 as any).foo } = o);
declare const n: number;
({ b = n.foo } = o);
({ b = (n as any as {}).foo } = o);
}
namespace N2 {
declare const o: { a?: number; b?: string };
let b: string;
({ b = undefined } = o);
declare const u: string | undefined;
({ b = u } = o);
declare const k: unknown;
({ b = k as never } = o);
}
namespace N3 {
declare const o: { a?: number; b?: string };
let b: string;
declare const k: unknown;
({ b = k } = o);
declare const v: void;
({ b = v } = o);
function f<T extends string | undefined, U extends string>(t: T, u: U) {
  ({ b = t } = o);
  ({ b = u } = o);
}
({ b = 5 as any } = o);
}
namespace N4 {
declare const o: { b?: string };
let b: string;
function f<T extends string | undefined>(t: T) {
  ({ b = t } = o);
}
function g<T extends string | undefined>(t: T, o2: { b: string }) {
  ({ b = t } = o2);
}
}
namespace N5 {
let fn: (x: number) => number;
({ fn = x => x } = {});
let lit: "a" | "b";
({ lit = "a" } = {});
let arr: [number, string];
({ arr = [1, "s"] } = {});
}
