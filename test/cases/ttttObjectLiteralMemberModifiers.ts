// @strict: true
// Modifiers on an object literal's members: TS1042 for each (checkGrammarObjectLiteralExpression), then a method's or
// accessor's are checked like a class member's (checkGrammarFunctionLikeDeclaration's checkGrammarModifiers: TS1244,
// TS1024, TS1028, TS1029, TS1040), and a method's TS1184 only when those pass; either ends the member's grammar checks
// (no '{' expected for a missing body). `readonly` makes the property readonly all the same (TS2540, TS2704)
const a1 = { abstract m() {} };
const a2 = { readonly m() {} };
const a3 = { public public m() {} };
const a4 = { public static m() {} };
const a5 = { static public m() {} };
const a6 = { async public m() {} };
const a7 = { declare async m() {} };
const a8 = { abstract e(): void };
const a9 = { readonly a: 1 };
a2.m = () => {};
a9.a = 2;
delete a2.m;
