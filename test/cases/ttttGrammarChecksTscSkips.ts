// Grammar errors tsc's checker never reports because an earlier check of the same node ended the chain or because it
// never visits the node: a class's or interface's heritage clauses after its modifiers fail (TS1044), the base's own
// type arguments and the type parameters after a complaint about the clauses (TS1172/TS1174), the heritage elements
// past the base, a variable statement's list after its modifiers fail, an import type's type arguments, a tagged
// template's type arguments after TS1358
// @filename: a.ts
declare class A<T = 1> {}
declare class D {}
interface I<T = 1> {}
declare const o: any;
public class B1 extends A, {}
public interface J1 extends I<1,>, {}
class B2 extends A extends A<> {}
class B3<> extends A extends A {}
class B4 extends A, class extends D, A {} {}
class B5 extends A, class<> {} {}
class B6 implements I implements (class extends D, A {}) {}
class B7 extends A<1,> {}
class B8<> extends A<> {}
public var v = 1,;
o?.a<>`x`;
o?.a<1,>`x`;
// @filename: b.ts
export interface G<T> { a: T }
type Q = import("./b").G<1,>;
