// @strict: true
// The arguments past a failed call's parameters aren't typed while the call is resolved: tsc checks them in the
// failed call's deferred resolveUntypedCall, after the declaration the call initializes has its type, so a reference
// to it there is no circularity (TS7022), only a use before declaration
declare function f(a: number): number;
const w = f(1, (() => w)());
declare function g<T>(a: T): T;
const v = g(1, (() => v)());
declare function h(a: number): number;
declare function h(a: string): string;
const u = h(1, (() => u)());
type ComponentProps<T> = T extends (props: infer P) => unknown ? P : never;
declare function wrap<P>(component: (props: P) => unknown): (props: P) => unknown;
const Wrapped = wrap((props: { a?: string }) => null, 1, ComponentProps<typeof Wrapped>);
