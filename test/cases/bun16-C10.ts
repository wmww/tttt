// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { kind: `a${number}`, f: (x: number) => void } | { kind: "b", f: (x: string) => void };
declare const n: number;
({ kind: `a${n}`, f: x => {} } as const) satisfies U;
const v: U = { kind: `a${n}`, f: x => {} } as const;
declare function fc<const T extends U>(x: T): void;
fc({ kind: `a${n}`, f: x => {} });
const sentinel: number = "";
