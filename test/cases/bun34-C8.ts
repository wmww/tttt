// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
({ [-0]: 1, 0: 2 });
({ [-0]: 1, "-0": 2 });
const sentinel: number = "";
