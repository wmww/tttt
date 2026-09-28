function f() { class C { x = 1 } return C; }
const c = f();
function g<T extends new (...args: any[]) => {}>(B: T) { class C extends B { x = 1 } return C; }
const d = g(class {});
