// A union member that is a conditional type over a type parameter constrained to an unresolved type has the error
// type as its apparent type, and says nothing about a property (tsc's createUnionOrIntersectionProperty skips it): a
// union of only such members has no property at all (crawlee's `cond ? this.#inner! : await this.#select()`,
// `Awaited<T> | NonNullable<T>` with `T extends IRequestManager` unresolved)
// @strict: true
// @target: esnext
import type { Nope } from "nope";
type Cond<T> = T extends string ? { foo: 1 } : { foo: 2 };
type Id<T> = T extends null ? never : T;
export function f<T extends Nope>(a: Awaited<T> | NonNullable<T>, b: Cond<T> | T, c: Id<T> | T, d: Awaited<T> | { foo: 1 }, e: Awaited<T>, g: Id<T> | { foo: 1 }) {
    a.foo;
    b.foo;
    c.foo;
    d.foo;
    e.foo;
    g.foo;
}
declare function select<T>(): Promise<T>;
export class C<T extends Nope> {
    #inner?: T;
    async m(cond: boolean) {
        const manager = cond ? this.#inner! : await select<T>();
        return (await manager.extend?.(1)) ?? false;
    }
}
// The members that have it make the property
export function g<T extends Nope>(d: T | { foo: 1 }, e: (T & {}) | { foo: 1 }) {
    const x = d.foo;
    const y = e.foo;
}
