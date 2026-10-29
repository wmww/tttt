// @strict: true
// @noEmit: true
// A loop's partial types are checked again in normal mode where tsc's checkTestingKnownTruthyTypes calls
// checkExpression: the right operand of a conditional's logical condition (and of each `||`/`??` down its left
// operands, a logical right operand recursively), and of a `&&`'s logical left operand. Elsewhere an assigned value's
// logical operands stay in CheckModeTypeOnly, whose equalities report nothing (opencode's scan.ts: `atBoundary ||
// (indexable === "member" && /[.:]$/.test(word)) ? "closed" : false`)
declare const more: boolean;
declare const s: string;
declare function f(): boolean;
function e1() { let x: string | number = "a"; while (more) { x = f() || (x === 5 && /x/.test(s)) ? "b" : 5; } }
function e2() { let x: string | number = "a"; while (more) { x = f() || x === 5 ? "b" : 5; } }
function e3() { let x: string | number = "a"; while (more) { x = f() && x === 5 ? "b" : 5; } }
function e4() { let x: string | number = "a"; let y = false; while (more) { x = (y = more && x === 5) ? "b" : 5; } }
function e5() { let x: string | number = "a"; let y = false; while (more) { x = (y = (more || x === 5) && more) ? "b" : 5; } }
function e6() { let x: string | number = "a"; while (more) { x = (f() || x === 5) || more ? "b" : 5; } }
function e7() { let x: string | number = "a"; while (more) { x = (s ?? x === 5) ? "b" : 5; } }
function e8() { let x: string | number = "a"; while (more) { x = f() || (more && x === 5) ? "b" : 5; } }
function e9() { let x: string | number = "a"; let y = false; while (more) { x = (y = more || x === 5) ? "b" : 5; } }
function e10() { let x: string | number = "a"; while (more) { x = (x === 5 || f()) ? "b" : 5; } }
function e11() { let x: string | number = "a"; while (more) { x = ((x === 5 && f()) || f()) ? "b" : 5; } }
function g() {
  let x: false | "closed" | "member" = false;
  for (let i = 0; i < 10; i++) {
    if (f()) { x = "closed"; continue; }
    if (f()) x = f() || (x === "member" && /x/.test(s)) ? "closed" : false;
    if (f()) x = "member";
  }
}
