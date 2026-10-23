// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
declare function g(): any;
let a: any;
({ [g(), "a"]: a } = {} as any);
const sentinel: number = "";
