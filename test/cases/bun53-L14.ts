// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class B {}
declare class A2 extends B, (class { m() {} }) {}
declare enum E4 { [(() => { return 1; })()] = 1 }
declare namespace N11 { class K extends B extends (class { m() {} }) {} }
declare namespace N12 { enum E { A = 1, [`x${(() => { return 1; })()}`] = 2 } }
const sentinel: number = "";
