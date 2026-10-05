interface Cls<Self, F> {
  new (props: F): F
}
declare const Class: <Self = never>(id: string) => [Self] extends [never] ? "missing" : Cls<Self, { a: string }>
class A extends Class<A>("A") {}
const a = new A({ a: "x" })
declare const Class2: <Self = never>() => Self extends string ? "missing" : Cls<Self, { a: string }>
class B extends Class2<B>() {}
const b = new B({ a: "x" })
declare function Class3<Self>(): Cls<Self, { a: string }>
class C extends Class3<C>() {}
const c = new C({ a: "x" })
export {}
