// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h() { export { xxx as www }; www; }
function h2() { export { yyy as vvv }; }
function h3() { export { zzz }; zzz; }
function h4() { const loc = 1; export { loc as uuu }; uuu; }
function h5() { export { ttt as sss }; let q: sss; }
function h6() { export { rrr as ppp }; let q: typeof ppp; }
namespace N { export { nnn as mmm }; mmm; }
{ export { bbb as ccc }; ccc; }
export {};
const sentinel: number = "";
// @filename: b.ts
{ export { b1 as b2 }; }
{ export { b3 as b4 }; b4; }
function hb() { export { b5 as b6 }; b6; }
