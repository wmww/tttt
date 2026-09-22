// @strict: true
interface A { readonly kind: "a"; readonly attributes?: { token: number } }
interface B { readonly kind: "b"; readonly attributes?: { token: number } }
function f(declaration: A | B) {
    const node = declaration.attributes;
    if (node) {
        declaration.attributes.token;
    }
}
function g(declaration: A) {
    const node = declaration.attributes;
    if (node) {
        declaration.attributes.token;
    }
}
interface C { attributes?: { token: number } }
function h(declaration: C) {
    const node = declaration.attributes;
    if (node) {
        declaration.attributes.token;
    }
}
