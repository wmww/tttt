// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class D1 { declare p = async () => {}; }
class D3 { declare p = function () {}; }
class D4 { declare p = { m() {}, get g() { return 1; } }; }
class D5 { declare p = class { m() {} }; }
class D6 { declare static p = () => {}; }
class D7 { declare readonly p = () => { let x = 1; }; }
class D8 { declare [(() => { return "a" as const; })()]: number; }
const E13 = class { declare p = () => {}; };
class D14 { declare p = (a = () => {}) => {}; }
const sentinel: number = "";
