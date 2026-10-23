// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function q1(a: number, ...r: [number, ...((x: number) => void)[][]]): void;
declare function q1(a: string, ...r: [string]): void;
q1(true, 1, x => { const z: 0 = x; });
const sentinel: number = "";
