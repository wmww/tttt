// @strict: true
// An evolving array's elements are regular object literals, not widened: `string` isn't a subtype of that `{}` (tsc's
// propertiesRelatedTo for an isObjectLiteralType target), the return type's widening then reduces `string | {}` to `{}`
// (First: tttt interns every `{}` literal as one type, so an earlier `{}` changes union order; tsc makes one per literal)
function f7(options?: { color: string, width: number }) {
    let { color, width } = options || {};
}
function f1() { let x = []; x.push("s"); x.push({}); return x; }
function f2() { return ["s", {}]; }
function f4() { let x = []; x.push("s"); x.push({ a: 1 }); return x; }
function g3() { let x = []; x.push({ a: 1 }); x.push({ a: 2, b: 2 }); return x; }
const o = {};
function f6() { let x = []; x.push("s"); x.push(o); return x; }
