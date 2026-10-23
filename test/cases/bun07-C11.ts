// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const l1: { [x: string]: number; [y: number]: number }; declare const r1: { [y: number]: 1; [x: string]: number };
const x1: null = { ...l1, ...r1 };
const sentinel: number = "";
