declare namespace A { throw
 1; }
declare namespace B { let x: number; throw
 1; }
function f() {
    throw
    1;
}
const sentinel: number = "";
