// `?.` after a parameter name is one token (tsc's scanner), not the optional marker: `,` expected at it, spanning both
// characters; `?.5` is `?` then a number
type T = {
    m: 1
if (fixed.thing?.name !== "Correct") {
}
declare function f(a?.5): void;
