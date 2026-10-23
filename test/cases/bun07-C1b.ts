// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function make<D, M>(o: { data: D; methods: M & ThisType<D & M> }): D & M;
const m = make({ data: { x: 1 }, methods: { get i() { return this.x; }, f() { return this.i; } } });
const sentinel: number = "";
