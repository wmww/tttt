// @noTypesAndSymbols: true
// A late-bound accessor symbol that conflicts with itself and with the written member of its name: each declaration is
// a duplicate under both names, the property name (lateBindMember) and `[k]` (combineSymbolTables' mergeSymbol).
// Types and symbols left out: tsc's merged symbol isn't mirrored (agents/bugs/bun_others.md)
const k = "a";
class C {
  get [k]() { return 1; }
  a() {}
}
class D {
  get [k]() { return 1; }
  set [k](v: number) {}
  [k]: string = "";
  a() {}
}
class E {
  get [k]() { return 1; }
  set [k](v: number) {}
  [k]: string = "";
  a() {}
  get [k]() { return 2; }
}
