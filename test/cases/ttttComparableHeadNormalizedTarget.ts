// @strict: true
// TS2678 and TS2352 name the pair as the relation normalized it (tsc's reportRelationError): a non-nullable source
// against a union of null or undefined and one other type is related to that type alone
declare const a: string | undefined;
switch (a) { case 1: break; }
declare const b: string | boolean;
switch (b) { case 1: break; }
declare const c: { x: number } | null;
switch (c) { case 1: break; }
const d = 1 as string | undefined;
const e = { x: "" } as { x: number } | null | undefined;
