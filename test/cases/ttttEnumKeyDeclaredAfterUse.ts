// @strict: true
// An enum member as an element access key names its property only once declared: tsc's tryGetNameFromEntityNameExpression
// reads an initializer only if isBlockScopedNameDeclaredBeforeUse, so `o[E.A]` above `const enum E` isn't narrowed
// (bun's 25-C2)
declare const o: { a: string | number };
if (typeof o[E.A] === "string") o[E.A].length;
const enum E { A = "a" }
const enum F { A = "a" }
if (typeof o[F.A] === "string") o[F.A].length;
