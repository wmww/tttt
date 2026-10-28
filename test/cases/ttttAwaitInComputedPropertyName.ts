// @module: esnext
// @target: es2022
// At a module's top level an `await` identifier in a property name doesn't make tsc reparse its statement in an await
// context (parsePropertyName saves and restores statementHasAwaitIdentifier): `[await]` stays an identifier, TS1262
// (the binder's isInTopLevelContext: a class member's computed name is outside the class, a type's names count too)
// and TS2304. `await.x` isn't an await expression either (a `.` starts a number only before a digit)
export {};
declare const x: any;
const { [await]: a } = x;
const o = { [await]: 1, [await.x]: 2 };
interface I { [await]: 1; [await.x]: 2 }
type T = { [await]: 1 };
class C { [await] = 1 }
