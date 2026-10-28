// The initializers in the parameters of a function type, a type member's signature or an ambient function (a binding
// pattern's too) are never checked: only TS2371 (tsc's checkVariableLikeDeclaration returns), and no parameter type
// is read from them. The grammar errors inside (a class expression's TS1174, TS1172, TS1098...) aren't reported. An
// overload's initializer types its parameter, so it is checked
declare class A {} declare class B {} interface J {}
type T1 = ({a = class extends A, B {}}: any) => void;
type T2 = ({a = class extends A extends B {}}: any) => void;
type T3 = ({a = class implements J implements J {}}: any) => void;
type T4 = ({a = class <> {}}: any) => void;
type T5 = (a = class { m<>() {} }) => void;
type T6 = new ({a = (public a) => 1}: any) => void;
interface I { m(a = class extends A, B {}): void; (b = class extends A, B {}): void; new (c = class extends A, B {}): I }
type L = { m({a = class extends A, B {}}: any): void };
declare function f(a = class extends A, B {}): void;
function g(a = class extends A, B {}): void;
function g() {}
