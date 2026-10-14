// @strict: true
// @target: es2015
// A default type argument that reads the type's own members while its bases are resolved (tsc's getBaseTypes failing
// popTypeResolution): TS2310 at the declaration, but the base is kept (only a base leading back is dropped), for
// interfaces and generic classes alike; an interface cycle drops the bases of both
interface BaseType<T> {
    bar: T
}
interface NextType<C extends { someProp: any }, T = C['someProp']> extends BaseType<T> {
    baz: string;
}
interface Foo extends NextType<Foo> {
    someProp: {
        test: true
    }
}
declare const foo: Foo;
foo.bar.test

class G<T> extends NextTypeC<G<T>> {
    someProp!: { x: T }
}
class NextTypeC<C extends { someProp: any }, T = C['someProp']> {
    bar!: T
}
new G<number>().bar.x;
interface A extends B {}
interface B extends A { b: number }
declare const a: A;
a.b;
