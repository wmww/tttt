//// [test/cases/ttttDeclarationComputedOverloadImplementation.ts] ////

//// [ttttDeclarationComputedOverloadImplementation.ts]
// A method with a computed name implementing overloads is left out of declaration emit like a written name's
// (tsc's isImplementationOfOverload by the late-bound symbol: mikro-orm's [Symbol.toPrimitive])
export class R {
  [Symbol.toPrimitive](hint: 'string'): string;
  [Symbol.toPrimitive](hint: string): string | never {
    return "";
  }
  ['lit'](a: 1): void;
  ['lit'](a: number): void {}
}


//// [ttttDeclarationComputedOverloadImplementation.js]
// A method with a computed name implementing overloads is left out of declaration emit like a written name's
// (tsc's isImplementationOfOverload by the late-bound symbol: mikro-orm's [Symbol.toPrimitive])
export class R {
    [Symbol.toPrimitive](hint) {
        return "";
    }
    ['lit'](a) { }
}


//// [ttttDeclarationComputedOverloadImplementation.d.ts]
export declare class R {
    [Symbol.toPrimitive](hint: 'string'): string;
    ['lit'](a: 1): void;
}
