// @strict: true
// A return type annotation that circularly references itself (TS2577) makes the signature's return type any, and the
// function's return statements are checked against that (bun 99-V4 checkReturnStatement__01/02: tsc's
// checkReturnStatement asks getReturnTypeOfSignature), not against the annotation as lowered once the cycle broke
export {}
function f(): [ReturnType<typeof f>] {
  return 1;
}
const probe: never = f();
class Chain {
  next(): ReturnType<Chain["next"]>[] {
    return "leaf";
  }
}
const probe2: never = new Chain().next();
function g(): [number] {
  return 1;
}
