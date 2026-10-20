// @target: es2015
// A failed call of a union's combined overloads says where the last one is declared: a union signature is a clone of
// the member signature it was made from (tsc's createUnionSignature), declared where that one is
declare var arr: string[] | number[];
arr.splice(1, "1");
interface A { f(x: string): void; f(x: number, y: number): void; }
interface B { f(x: string): void; f(x: number, y: number): void; }
declare var ab: A | B;
ab.f(true);
