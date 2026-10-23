// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: any;
o?.a`x`;
o?.a`x``y`;
o?.a`x`.b`y`;
o?.a`x${1}``y${2}`;
o?.a`x`()`y`;
o?.a`x`[0]`y`;
o?.a`x`!`y`;
(o?.a`x`)`y`;
o?.a`x`.b.c;
o?.a<string>`x``y`;
const sentinel: number = "";
