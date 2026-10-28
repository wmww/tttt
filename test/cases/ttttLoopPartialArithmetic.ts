// A loop's unfinished flow analysis checks an assigned arithmetic's operands on the partial types (tsc's
// getTypeOfExpression is a full check): `t = t + n` with `let t;` is TS18048 though t's final type is any
// @strict: true
// @noEmit: true
let t;
for (const n of [1, 2]) { t = t + n; }
let u;
for (const n of [1, 2]) { u = n + u; }
let w = undefined;
while (w !== 3) { w = w * 2; }
let s;
for (const n of [1, 2]) { s = s ? s + n : n; }
declare function cond(): boolean;
{
  let iNext;
  for (let i = undefined; i < 10; i = iNext) {
    if (cond()) {
      iNext = "bad";
      continue;
    }
    iNext = i + 1;
    let k = i;
  }
}
{
  let iNext;
  for (let i = undefined; i < 10; i = iNext) {
    iNext = i + 1;
    let k = i;
  }
}
