// A class member whose modifiers (checkGrammarModifiers, in the parser) or signature (parameter list, accessor,
// generator checks) fail tsc's grammar checks gets no TS1166/TS1168/TS1171 for its computed name (bun_others.md)
// @strict: true
// @target: es2022
declare var a: string, b: string;
class C {
    public public [a.length, b]: number;
    static public [a.length, b]: number;
    public [a.length, b](...x: number[], y: number) {}
    get [a.length, b](x: number) { return 1; }
    set [a.length, b]() {}
    get [a.length, b]<T>() { return 1; }
    private [a.length, b]?(a?: number = 1) {}
    async [a.length, b]: number;
    [a.length, b](x: number) {}
    declare [a.length, b](): void;
    accessor [a.length, b]() {}
    readonly [a.length, b]() {}
    export [a.length, b]: number;
}
class D {
    [a.length, b](x: number) {}
    get [a.length, b]() { return 1; }
    m<T>(...x: number[]) {}
    *[a.length, b]() {}
    [a.length, b](): void;
}
