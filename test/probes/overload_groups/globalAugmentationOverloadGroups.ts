// @filename: globals.d.ts
declare function f(handler: string | Function): number;

// @filename: aug.d.ts
export {};
declare global {
    function g(callback: () => void): string;
    namespace g { const x: number; }
    function f(callback: () => void): string;
    namespace f { const y: number; }
}

// @filename: main.ts
// The later declarations' overloads are tried first, whatever was resolved before
const b: string = g(() => {});
const a: string = f(() => {});
