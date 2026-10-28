// @strict: true
// @target: es2022
// TS1171 (tsc's checkGrammarComputedPropertyName) comes after a member's other grammar checks: a class property's or
// overload's TS1166/TS1168, an interface's or type literal's TS1169/TS1170 (a comma expression is no entity name, so
// only an accessor's TS1171 is left there); and a type member's `[a, b]` parses like a class member's (bun 02-C10)
declare function f(): void;
declare const s: string;
class C1 { [f(), "a"]() {} }
class C2 { get [f(), "b"]() { return 1 } }
class C3 { [f(), "e"] = 1; [f(), s] = 2; [f(), "g"](): void; [f(), "g"]() {} }
interface I1 { get [f(), "c"](): number }
interface I2 { [f(), "d"]: number; [f(), "e"](): void; [f(), s]: number }
type T = { [f(), "f"]: number; set [f(), "h"](v: number) };
