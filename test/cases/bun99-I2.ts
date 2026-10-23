// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
type Hydrated<T> = T extends { x: {} } ? T : T & { [P in keyof T]: T[P] extends Lazy<infer U> ? Lazy<U> : T[P] };
declare class Lazy<T> { brand: T }
declare class Collection<T> { hydrate(): Hydrated<T> }
declare class M1 { assert(): this is Hydrated<this>; }
class C1 extends Collection<M1> {}
declare function r1<T extends M1>(ctor: new () => Collection<NoInfer<T>>): void;
r1(C1);
declare class M2 { self(): this; assert(): this is Hydrated<this>; }
class C2 extends Collection<M2> {}
declare function r2<T extends M2>(ctor: new () => Collection<NoInfer<T>>): void;
r2(C2);
declare class M3 { assert(): this is Hydrated<this>; self(): this; }
class C3 extends Collection<M3> {}
declare function r3<T extends M3>(ctor: new () => Collection<NoInfer<T>>): void;
r3(C3);
declare class M4 { other(): void; assert(): this is Hydrated<this>; }
class C4 extends Collection<M4> {}
declare function r4<T extends M4>(ctor: new () => Collection<NoInfer<T>>): void;
r4(C4);
declare class M5 { n: number; assert(): this is Hydrated<this>; }
class C5 extends Collection<M5> {}
declare function r5<T extends M5>(ctor: new () => Collection<NoInfer<T>>): void;
r5(C5);
declare class M6 { other(): void; self(): this; assert(): this is Hydrated<this>; }
class C6 extends Collection<M6> {}
declare function r6<T extends M6>(ctor: new () => Collection<NoInfer<T>>): void;
r6(C6);
declare class M7 { self(): this; other(): void; assert(): this is Hydrated<this>; }
class C7 extends Collection<M7> {}
declare function r7<T extends M7>(ctor: new () => Collection<NoInfer<T>>): void;
r7(C7);
declare class M8 { is(): this is {}; assert(): Hydrated<this>; }
class C8 extends Collection<M8> {}
declare function r8<T extends M8>(ctor: new () => Collection<NoInfer<T>>): void;
r8(C8);
declare class M9 { assert(): this is Hydrated<this>; again(): this is Hydrated<this>; }
class C9 extends Collection<M9> {}
declare function r9<T extends M9>(ctor: new () => Collection<NoInfer<T>>): void;
r9(C9);
declare function value<T extends M2>(c: Collection<NoInfer<T>>): void;
value(new C2());
const sentinel: number = "";
