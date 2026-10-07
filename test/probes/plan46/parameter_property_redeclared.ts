class C {
    constructor(public names: number);
    constructor(public names: string, x?: number) {}
}
class D {
    names: number;
    constructor(public names: string) {}
}
class E {
    constructor(public a: number, private b?: string);
    constructor(public a: number, private b?: string) {}
}
