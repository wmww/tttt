// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function observable<T>(o: T): T;
const store = observable({
  items: [{ done: true }],
  get doneItems() { return this.items.filter(x => x.done); },
  add() { this.items.push({ done: false }); },
});
const sentinel: number = "";
