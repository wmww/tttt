// @strict: true
// @allowUnreachableCode: false
type Shape = { kind: "circle"; r: number } | { kind: "square"; s: number } | { kind: "tri"; a: number; b?: number };
class A { a = 1; isB(): this is B { return false; } }
class B extends A { b = 2; }
class C { c = 3; }
declare function isString(x: unknown): x is string;
declare function isA(x: unknown): x is A;
declare function assert(value: unknown, message?: string): asserts value;
declare function assertIsNumber(x: unknown): asserts x is number;
declare function fail(message?: string): never;
declare function use(x: unknown): void;

function predicates(x: string | number | undefined, y: A | C, u: unknown) {
    if (isString(x)) { x; } else { x; }
    if (!isString(x)) { x; return; }
    x;
    if (isA(y)) { y; } else { y; }
    if (isA(u)) { u; } else { u; }
    if (isString(u) === true) { u; }
    if (isString(u) !== true) { u; } else { u; }
}
function thisPredicate(a: A) {
    if (a.isB()) { a; a.b; } else { a; }
}
function assertions(x: string | number | undefined, s: Shape) {
    assert(x);
    x;
    assertIsNumber(x);
    x;
    assert(s.kind === "circle" || s.kind === "square");
    s;
    assert(false);
    x;
}
function neverCalls(x: string | undefined): string {
    if (x === undefined) fail("no");
    x;
    return x;
}
function instanceOf(v: A | C | string, w: unknown) {
    if (v instanceof A) { v; } else { v; }
    if (v instanceof B) { v; } else { v; }
    if (w instanceof C) { w; } else { w; }
}
function inOperator(s: Shape, o: { a: number } | { b: string }) {
    if ("r" in s) { s; } else { s; }
    if ("b" in s) { s; } else { s; }
    if ("a" in o) { o; } else { o; }
}
function discriminants(s: Shape, o?: Shape) {
    if (s.kind === "circle") { s; s.r; } else { s; }
    if (s.kind !== "tri") { s; } else { s.a; }
    if (o?.kind === "square") { o; }
    if (s.b) { s; }
    switch (s.kind) {
        case "circle": s; break;
        case "square":
        case "tri": s; break;
        default: s;
    }
}
function area(s: Shape): number {
    switch (s.kind) {
        case "circle": return s.r;
        case "square": return s.s;
        case "tri": return s.a;
    }
}
function notExhaustive(s: Shape): number {
    switch (s.kind) {
        case "circle": return s.r;
        case "square": return s.s;
    }
}
function switchTypeof(x: string | number | boolean | (() => void)) {
    switch (typeof x) {
        case "string": x; break;
        case "number":
        case "boolean": x; break;
        default: x;
    }
}
function switchTrue(x: string | number | undefined) {
    switch (true) {
        case typeof x === "string": x; break;
        case x === undefined: x; break;
        default: x;
    }
}
function switchValue(x: "a" | "b" | 1 | undefined) {
    switch (x) {
        case "a": x; break;
        case 1: x; break;
        default: x;
    }
    x;
}
function afterNever(x: number) {
    fail();
    use(x);
}
const filtered = ["a", 1, undefined].filter(isString);
