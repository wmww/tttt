// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function q9(a: number, ...r: [number] | [string, (x: number) => void]): void;
declare function q9(a: string, ...r: ((x: string) => void)[]): void;
q9(true, "", x => { const z: 0 = x; });
const sentinel: number = "";
