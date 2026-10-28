// @target: es2022
// tsc's checkGrammarModifiers: an async arrow function in an ambient context is TS1040 (`declare const f = async () =>
// {}`, a `declare namespace`'s); a class's decorated index signature is TS1206 alone, which ends the check (no TS1071)
declare const x: any = async () => { };
declare namespace N { const z: any = async () => 1; }
declare const dec: any;
class A { @dec public [k: string]: any }
// An index signature's parameter modifiers are a parameter's (tsc's checkParameter): `in`/`out` TS1274, `async` TS1090
type W1 = { [in K: string]: 1 };
type W2 = { [out K: string]: 1 };
type W5 = { [async K: string]: 1 };
