// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "noErrorTruncation": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let p1: (x: any) => x is string = null! as (b: any) => b is { aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: 1; bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb: 2; cccccccccccccccccccccccccccccccccccccccc: 3; dddddddddddddddddddddddddddddddddddddddd: 4; e: 5; f: 6; g: 7; h: 8; i: 9 };
const sentinel: number = "";
