// @strict: true
// @target: es2015
// tsc relates a satisfies expression while the declaration's type is computed (checkSatisfiesExpression is part of
// checkExpression), and a failed relation's elaboration reads the type of every property of the literal the target
// has (elaborateElement), the getter's return type included: that meets the declaration in flight, TS7022 + TS7023.
// tttt ran the relation then, not the elaboration (the noCircularitySelfReferentialGetter3/4 mutants).
const a = { prop: "42", get self() { return a; } } satisfies { prop: number; self: any };
const b = { get self() { return b; }, prop: "42" } satisfies { prop: number; self: any };
const c = { inner: { prop: "42", get self() { return c; } } } satisfies { inner: { prop: number; self: any } };
const d = [{ prop: "42", get self() { return d; } }] satisfies { prop: number; self: any }[];
// The target has no `self`, or a method: nothing reads a circular type
const e = { prop: "42", get self() { return e; } } satisfies { prop: number };
const f = { prop: "42", self() { return f; } } satisfies { prop: number; self: any };
// The relation alone reads it (against `string`, not `any`): circular, then the any fits, no TS2322
const g = { prop: 42, get self() { return g; } } satisfies { prop: number; self: string };
// Passing: nothing is circular
const h = { prop: 42, get self() { return h; } } satisfies { prop: number; self: any };
