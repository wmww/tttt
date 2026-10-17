// @strict: true
// The properties of an intersection target see the whole intersection as their `this` (tsc's getPropertiesOfType
// through getApparentTypeOfIntersectionType), like the source's do: `self()` of `Mark<B>` returns `Mark<B>`

declare class B {
    tag: string;
    self(): this;
}
type Mark<T> = T & { mark: true };

interface S {
    tag: string;
    mark: true;
    self(): { tag: string; self(): any };
}
declare let s: S;
let t: Mark<B> = s;

interface Ok {
    tag: string;
    mark: true;
    self(): Ok;
}
declare let ok: Ok;
let u: Mark<B> = ok;
