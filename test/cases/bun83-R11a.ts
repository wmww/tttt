// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a01.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { var; } : 1;
const sentinel: number = "";
// @filename: a02.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { declare let; } : 1;
const sentinel: number = "";
// @filename: a03.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { const; } : 1;
const sentinel: number = "";
// @filename: a05.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { var b = 1,; } : 1;
const sentinel: number = "";
// @filename: a07.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { export var; } : 1;
const sentinel: number = "";
// @filename: a08.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => function () { var; } : 1;
const sentinel: number = "";
// @filename: a09.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => class { m() { var; } } : 1;
const sentinel: number = "";
// @filename: a10.ts
export {}; declare const x: any; type T = any;
const v = x ? (a): T => { var; } : (b): T => { var; };
const sentinel: number = "";
// @filename: a11.ts
export {}; declare const x: any; type T = any;
const v = x ? async (a): T => { var; } : 1;
const sentinel: number = "";
// @filename: a13.ts
export {}; declare const x: any; type T = any;
const v = x ? x ? (a): T => { var; } : 1 : 2;
const sentinel: number = "";
// @filename: c01.ts
export {}; declare const f: any, x: any;
const v = (a): { [(() => { var; return "x" })()]: 1 } => x;
const sentinel: number = "";
// @filename: c02.ts
export {}; declare const f: any, x: any;
const v = f<{ [(() => { var; return "x" })()]: 1 }>(x);
const sentinel: number = "";
