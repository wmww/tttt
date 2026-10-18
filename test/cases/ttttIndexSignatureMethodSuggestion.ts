// @noImplicitAny: true
// TS7052's suggestion names the object as tsc's tryGetPropertyAccessOrIdentifierToString does: bracket accesses with
// a property name key (an identifier, a string, a number as JavaScript prints it) as dots, parentheses as nothing
interface M { get(k: string): number; set(k: string, v: number): void }
declare const field: { group: M[]; m: M };
declare const k: string;
field.group[0]["x"];
field.group[0x10]["x"];
field["m"]["x"];
field.m[k];
(field).m["x"];
field.group[1.50]["x"] = 1;
