// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function foo<T>(): string;
declare function base<T>(): new () => {};
class C1<Item> { static m(x: Itm) {} }
class C4<Item> { [foo<Itm>()]() {} }
class C5<Item> extends base<Itm>() {}
const sentinel: number = "";
