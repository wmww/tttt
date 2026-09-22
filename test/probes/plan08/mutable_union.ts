// @strict: true
type Mutable<T extends object> = { -readonly [K in keyof T]: T[K]; };
interface A { readonly kind: 1; readonly a: string; readonly flags: number }
interface B { readonly kind: 2; readonly b: number; readonly flags: number }
declare function has(n: A | B): boolean;
function set(node: Mutable<A | B>) {
    if (has(node)) node.flags |= 1;
    const k = node.kind;
}
declare const b: B;
set(b);
