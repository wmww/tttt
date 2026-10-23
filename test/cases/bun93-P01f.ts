// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "target": "es2022" } }
// @filename: a.ts
export {};
declare const p: Promise<string>, q: Promise<number>, x: any;
({ m() {}await  } = x);
const sentinel: number = "";
