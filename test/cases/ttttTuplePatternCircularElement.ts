// @strict: true
// An annotated pattern's first name is resolved before its initializer is checked, and under strictNullChecks asks for
// the initializer's type; the function in it infers its return type at that check: `d` read there is TS7022
const [d, e]: [number, (n: number) => number] = [1, (n) => e(n) + d];
const [d2, e2]: [number, (n: number) => number] = [1, (n: number) => e2(n) + d2];
const { d3, e3 }: { d3: number, e3: (n: number) => number } = { d3: 1, e3: (n) => e3(n) + d3 };
const [d5, e5]: [number, () => number] = [1, () => d5];
// A name read again by the initializer's second check isn't: its function was contextually checked once
const { c6, f6 }: { c6: 0; f6: () => unknown } | { c6: 1; f6: string } = { c6: 0, f6: () => f6 };
