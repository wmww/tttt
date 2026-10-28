// @strict: true
// A property declared again without a type is typed by its first declaration (the symbol's valueDeclaration): TS2717
// for the second, no TS7008, which tsc reports only while widening the valueDeclaration's type
class C {
    public "1": string = "number";
    1;
    x;
    x: number;
}
