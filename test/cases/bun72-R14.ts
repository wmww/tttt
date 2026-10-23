// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function id<T>(x: T): T;
type R = (x: number) => string;
const objv = { z: 1, cb: (): R => null! };
const { z, ...rest1 } = objv; declare function h1<T>(a: T, o: typeof rest1): T; h1(1, { cb: () => id });
const sp1 = { ...objv }; declare function h2<T>(a: T, o: typeof sp1): T; h2(1, { z: 1, cb: () => id });
const sp2 = { ...objv, k: 1 }; declare function h3<T>(a: T, o: typeof sp2): T; h3(1, { z: 1, k: 1, cb: () => id });
let wid = { cb: (): R => null!, n: null }; declare function h4<T>(a: T, o: typeof wid): T; h4(1, { n: null, cb: () => id });
const asg = Object.assign({ q: 1 }, objv); declare function h5<T>(a: T, o: typeof asg): T; h5(1, { q: 1, z: 1, cb: () => id });
const cex = class { cb!: () => R }; declare function h6<T>(a: T, o: InstanceType<typeof cex>): T; h6(1, { cb: () => id });
enum E { A } namespace E { export const cb: () => R = null! } declare function h7<T>(a: T, o: typeof E): T; h7(1, { A: E.A, cb: () => id });
namespace V { export const cb: () => R = null! } declare function h8<T>(a: T, o: typeof V): T; h8(1, { cb: () => id });
function FN() {} namespace FN { export const cb: () => R = null! } declare function h9<T>(a: T, o: Pick<typeof FN, "cb">): T; h9(1, { cb: () => id });
function withRest({ z, ...r }: typeof objv) { function h<T>(a: T, o: typeof r): T { return a } h(1, { cb: () => id }); }
const [, ...arest] = [1, (): R => null!] as [number, () => R]; declare function h10<T>(a: T, o: typeof arest): T; h10(1, [() => id]);
declare const u: { z: 1; cb: () => R } | { z: 2; cb: () => R; w: 1 }; const { z: z2, ...rest2 } = u; declare function h11<T>(a: T, o: typeof rest2): T; h11(1, { cb: () => id });
const { ...rest3 } = objv; declare function h12<T>(a: T, o: typeof rest3): T; h12(1, { z: 1, cb: () => id });
type RestAlias = typeof rest1; declare function h13<T>(a: T, o: RestAlias): T; h13(1, { cb: () => id });
declare function h14<T>(a: T, o: (typeof rest1)[]): T; h14(1, [{ cb: () => id }]);
declare function h15<T>(a: T, o: typeof rest1 | undefined): T; h15(1, { cb: () => id });
declare function h16<T>(a: T, o: (typeof rest1)["cb"]): T; h16(1, () => id);
let r5; ({ z: r5, ...r5 } = objv);
const nested = { in: rest1 }; declare function h17<T>(a: T, o: typeof nested): T; h17(1, { in: { cb: () => id } });
const sentinel: number = "";
