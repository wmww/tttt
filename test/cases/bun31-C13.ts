// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noImplicitThis": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...x: any[]): any;
declare function pick<T>(other: (t: T) => void, cb: () => T): void;
pick(t => { t.toFixed(); }, function () { class C { @dec(this) m() {} } return 1; });
const sentinel: number = "";
