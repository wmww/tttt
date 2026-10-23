// @strict: true
// A return type annotation that infers from the function's own type is circular (TS2577, the call any): the
// annotation isn't lowered again from inside its own lowering (bun's 99-V4 overflowed the stack)
function f(): typeof f extends () => infer R ? R : never {
    return null!;
}
const result = f();
const probe: never = result;
