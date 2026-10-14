// @target: es2015
// `fx<>` instantiates with the defaults (unknown), `typeof fx<>` too and reprints as `typeof fx`; `typeof f0` of an instantiated f0 prints structurally
declare function fx<T>(x: T): T;
type T10 = typeof fx<>;
type T11 = typeof fx;
const f0 = fx<>;
type T12 = typeof f0;
