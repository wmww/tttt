// @noImplicitAny: true
// An untyped property signature declared again (in one type literal, or in a later declaration of a merged
// interface) is TS7008 once, at its symbol's first declaration: tsc reports implicit any while widening the
// valueDeclaration's type. Seed 20261117 --syntax, signatureCombiningRestParameters1 delete-stmt
type A = { x; x; y: number; y };
interface I { z; }
interface I { z; }
type M = { m(): void; m };
