// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: i03.ts
var v = new X<T extends U>(a);
// @filename: i13.ts
var v = new <T extends U>;
// @filename: j01.ts
var v = f<T extends U>(a);
// @filename: j06.ts
var v = f<typeof>(x);
// @filename: j10.ts
var v = f<T extends U ? 1>(x);
// @filename: j13.ts
var v = f<T[>(x);
// @filename: j16.ts
var v = f<new>(x);
// @filename: j17.ts
var v = f<infer>(x);
// @filename: j19.ts
var v = f<T extends U>`x`;
// @filename: j20.ts
var v = f<T extends U>;
// @filename: j21.ts
var v = f<T extends U>.x;
// @filename: j23.ts
var v = a.b<T extends U>(x);
// @filename: j32.ts
var v = f<import>(x);
// @filename: j33.ts
var v = f<import(>(x);
// @filename: j34.ts
var v = f<`${>(x);
// @filename: j40.ts
var v = f<abstract new>(x);
