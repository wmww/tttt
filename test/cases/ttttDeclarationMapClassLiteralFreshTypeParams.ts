// @declaration: true
// @declarationMap: true
// @strict: true
// A class instance printed as a type literal: tsgo instantiates its members for `this` (resolveClassOrInterfaceMembers)
// except the thisless ones, and isThisless wants a single declaration, so an overloaded generic method's signatures
// get fresh type parameters and the parameters mentioning them are printed from their types, unmapped
// (babel-traverse's `const NodePath_Final = class NodePath { ... }`): getObjectTypeInstantiation only instantiates the
// method type where a declaration possibly references `this` (an implementation without a return type does).
export interface V<S> { s: S }
export const C = class N {
    t<S>(x: V<S>, s: S): void;
    t(x: V<any>): void;
    t(x: any, s?: any) {}
    u<T>(x: V<T>): void {}
    w<T>(x: V<T>) {}
};
// A method whose declarations don't possibly reference `this` (each with a return type) isn't instantiated: mapped
export const D = class M {
    o<S>(x: V<S>, s: S): void;
    o(x: V<any>): void;
    o(x: any, s?: any): void {}
    q<T>(x: V<T>, z: string | number): T { return null!; }
};
