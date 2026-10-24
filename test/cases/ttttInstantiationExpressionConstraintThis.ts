// A signature's type arguments (an instantiation expression's, a call's) are related to their constraints with `this`
// the argument (tsc's checkTypeArguments), a type reference's without (checkTypeArgumentConstraints)
// @strict: true
// @noEmit: true
declare class A { m(): this; }
declare class B { m(): this; }
declare class Box<T extends A> { t: T }
type Ref<RQ extends typeof B> = Box<InstanceType<RQ>>;
declare function k<RQ extends typeof B>(): typeof Box<InstanceType<RQ>>;
declare function k2<RQ extends typeof B, S extends typeof Box<InstanceType<RQ>> = typeof Box>(): S;
declare const box: typeof Box;
function use<RQ extends typeof B>() {
    const b = box<InstanceType<RQ>>;
}
