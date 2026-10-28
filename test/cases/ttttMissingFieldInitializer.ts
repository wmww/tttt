// A class field's or enum member's missing initializer (`x = ;`) is an initializer (tsc's missing identifier, any):
// no TS7008 or TS2564, the next enum member follows a computed one (TS1061), the types show it
// @strict: true
// @noImplicitAny: true
class C<U> {
    private x = ;
    y: U = ;
    z: number = ;
    static s = ;
    w;
}
enum E { A = , B }
enum F { A = 1, B = , C }
const c = new C<string>();
let n: number = E.B;
