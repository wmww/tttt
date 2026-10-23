// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const {} = miss1;
declare const [] = miss2;
declare let {} = miss3;
declare const {}: {} = miss4;
declare namespace N { const {} = miss5; }
declare const {} = class { declare x = 1 };
declare const {} = (() => { const q: number = ""; })();
const sentinel: number = "";
