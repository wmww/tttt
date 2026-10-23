// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare let on: { n: number; m(): void };
declare function f(): "m";
({ [r1]() {} } = on);
({ get [r2]() { return 1; } } = on);
({ [f(1)]() {} } = on);
({ [r3]() {} } = { m() {} });
const sentinel: number = "";
