// @strict: true
// A static field named `prototype` merges into the class's prototype property (tsc's binder), whose type is the
// instance type with any type arguments: its initializer is checked against that, not against its annotation (bun
// 99-V4 getTypeOfPrototypeProperty__02)
class C { static prototype = 1; x = ""; }
class D { static prototype: number = 1; }
class E { static prototype: E = new E(); }
class G<T> { static prototype = { a: 1 }; t!: T; }
class H { static prototype: string; }
const c = class { static prototype = 2 };
class K { static prototype: number = "s"; }
class J { x = 1; static prototype: number = 1; }
class L { y = 1; static prototype: { y: number } = { y: 2 }; }
const probe: never = C.prototype;
