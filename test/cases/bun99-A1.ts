// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "target": "ES2022", "lib": ["ES2022"], "noEmit": true, "types": [], "skipLibCheck": true } }
// @filename: a.ts
interface P<T> { v: T; then<W = never>(f: (value: T) => unknown): P<W>; both<U, W = never>(f: (value: T) => U | P<U>): P<U | W>; one<U>(f: (value: T) => U | P<U>): P<U> }
interface Box<A, R> { readonly a: A; readonly r: R }
declare const start: P<number>;
declare function run<A, R>(box: Box<A, R>): P<A[]>;
declare function make1<R>(runner: <A>(box: Box<A, R>) => P<A[]>, tag: R): void;
{ const r = make1((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make1((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make1((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make1((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make1(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make1((box) => run(box), "tag"); const probe: never = r; }
{ const r = make1((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make1(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make1((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make1(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make2<R>(runner: <A extends {}>(box: Box<A, R>) => P<A[]>, tag: R): void;
{ const r = make2((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make2((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make2((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make2((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make2(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make2((box) => run(box), "tag"); const probe: never = r; }
{ const r = make2((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make2(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make2((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make2(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make3<R>(runner: <A extends unknown>(box: Box<A, R>) => P<A[]>, tag: R): void;
{ const r = make3((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make3((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make3((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make3((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make3(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make3((box) => run(box), "tag"); const probe: never = r; }
{ const r = make3((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make3(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make3((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make3(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make4(runner: <A>(box: Box<A, string>) => P<A[]>, tag: string): void;
{ const r = make4((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make4((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make4((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make4((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make4(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make4((box) => run(box), "tag"); const probe: never = r; }
{ const r = make4((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make4(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make4((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make4(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make5<R>(runner: <A>(box: Box<A, string>) => P<A[]>, tag: R): void;
{ const r = make5((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make5((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make5((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make5((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make5(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make5((box) => run(box), "tag"); const probe: never = r; }
{ const r = make5((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make5(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make5((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make5(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make6<R>(tag: R, runner: <A>(box: Box<A, R>) => P<A[]>): void;
{ const r = make6("tag", (box) => start.then(() => box)); const probe: never = r; }
{ const r = make6("tag", (box) => start.both(() => run(box))); const probe: never = r; }
{ const r = make6("tag", (box) => start.one(() => run(box))); const probe: never = r; }
{ const r = make6("tag", (box) => { return start.then(() => box); }); const probe: never = r; }
{ const r = make6("tag", function (box) { return start.then(() => box); }); const probe: never = r; }
{ const r = make6("tag", (box) => run(box)); const probe: never = r; }
{ const r = make6("tag", (box) => (box.a ? start.then(() => box) : run(box))); const probe: never = r; }
{ const r = make6("tag", () => start.then(() => 0)); const probe: never = r; }
{ const r = make6("tag", (box) => start.one(() => box)); const probe: never = r; }
{ const r = make6("tag", <Z,>(box: Box<Z, string>) => start.both(() => run(box))); const probe: never = r; }
declare function make7<R>(o: { runner: <A>(box: Box<A, R>) => P<A[]> }, tag: R): void;
{ const r = make7({ runner: (box) => start.then(() => box) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => start.both(() => run(box)) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => start.one(() => run(box)) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => { return start.then(() => box); } }, "tag"); const probe: never = r; }
{ const r = make7({ runner: function (box) { return start.then(() => box); } }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => run(box) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => (box.a ? start.then(() => box) : run(box)) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: () => start.then(() => 0) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: (box) => start.one(() => box) }, "tag"); const probe: never = r; }
{ const r = make7({ runner: <Z,>(box: Box<Z, string>) => start.both(() => run(box)) }, "tag"); const probe: never = r; }
declare function make8<R>(runner: <A, B>(box: Box<A, R>, other: B) => P<A[]>, tag: R): void;
{ const r = make8((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make8((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make8((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make8((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make8(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make8((box) => run(box), "tag"); const probe: never = r; }
{ const r = make8((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make8(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make8((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make8(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make9<R>(tag: R, runner?: <A>(box: Box<A, R>) => P<A[]>): void;
{ const r = make9("tag", (box) => start.then(() => box)); const probe: never = r; }
{ const r = make9("tag", (box) => start.both(() => run(box))); const probe: never = r; }
{ const r = make9("tag", (box) => start.one(() => run(box))); const probe: never = r; }
{ const r = make9("tag", (box) => { return start.then(() => box); }); const probe: never = r; }
{ const r = make9("tag", function (box) { return start.then(() => box); }); const probe: never = r; }
{ const r = make9("tag", (box) => run(box)); const probe: never = r; }
{ const r = make9("tag", (box) => (box.a ? start.then(() => box) : run(box))); const probe: never = r; }
{ const r = make9("tag", () => start.then(() => 0)); const probe: never = r; }
{ const r = make9("tag", (box) => start.one(() => box)); const probe: never = r; }
{ const r = make9("tag", <Z,>(box: Box<Z, string>) => start.both(() => run(box))); const probe: never = r; }
declare function make10<R>(runner: (<A>(box: Box<A, R>) => P<A[]>) | undefined, tag: R): void;
{ const r = make10((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make10((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make10((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make10((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make10(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make10((box) => run(box), "tag"); const probe: never = r; }
{ const r = make10((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make10(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make10((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make10(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make11<R>(runner: <A>(box: Box<A, R>) => P<A[]>, tag: R): typeof runner;
{ const r = make11((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make11((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make11((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make11((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make11(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make11((box) => run(box), "tag"); const probe: never = r; }
{ const r = make11((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make11(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make11((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make11(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function make12<R>(runner: <A>(box: Box<A, R>) => P<A[]>, tag: R): void;
{ const r = make12<string>((box) => start.then(() => box), "tag"); const probe: never = r; }
{ const r = make12<string>((box) => start.both(() => run(box)), "tag"); const probe: never = r; }
{ const r = make12<string>((box) => start.one(() => run(box)), "tag"); const probe: never = r; }
{ const r = make12<string>((box) => { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make12<string>(function (box) { return start.then(() => box); }, "tag"); const probe: never = r; }
{ const r = make12<string>((box) => run(box), "tag"); const probe: never = r; }
{ const r = make12<string>((box) => (box.a ? start.then(() => box) : run(box)), "tag"); const probe: never = r; }
{ const r = make12<string>(() => start.then(() => 0), "tag"); const probe: never = r; }
{ const r = make12<string>((box) => start.one(() => box), "tag"); const probe: never = r; }
{ const r = make12<string>(<Z,>(box: Box<Z, string>) => start.both(() => run(box)), "tag"); const probe: never = r; }
declare function real<R>(runner: <A>(box: Box<A, R>) => Promise<A[]>, tag: R): void;
declare function runReal<A, R>(box: Box<A, R>): Promise<A[]>;
real((box) => Promise.resolve(0).then(() => runReal(box)), "tag");
real(async (box) => await runReal(box), "tag");
const sentinel: number = "";
export {};
