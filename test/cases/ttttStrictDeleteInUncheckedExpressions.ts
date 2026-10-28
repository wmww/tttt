// @strict: true
// tsc's binder visits what its checker never checks: `delete x` (TS1102) in a function declaration's invalid
// decorator, a heritage element past TS1174, an implemented or extended expression that isn't a name (TS2500/TS2499)
declare var x: any;
@(delete x) function f() {}
class C extends Object, (delete x) {}
interface I extends (delete x) {}
class D implements (delete x) {}
