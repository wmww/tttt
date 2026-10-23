// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: f0.ts
export const x = 1;
// @filename: f1.ts
export { x } from "./f0";
// @filename: f10.ts
export { x } from "./f9";
// @filename: f11.ts
export { x } from "./f10";
// @filename: f12.ts
export { x } from "./f11";
// @filename: f13.ts
export { x } from "./f12";
// @filename: f14.ts
export { x } from "./f13";
// @filename: f15.ts
export { x } from "./f14";
// @filename: f16.ts
export { x } from "./f15";
// @filename: f17.ts
export { x } from "./f16";
// @filename: f18.ts
export { x } from "./f17";
// @filename: f19.ts
export { x } from "./f18";
// @filename: f2.ts
export { x } from "./f1";
// @filename: f20.ts
export { x } from "./f19";
// @filename: f21.ts
export { x } from "./f20";
// @filename: f22.ts
export { x } from "./f21";
// @filename: f23.ts
export { x } from "./f22";
// @filename: f24.ts
export { x } from "./f23";
// @filename: f25.ts
export { x } from "./f24";
// @filename: f26.ts
export { x } from "./f25";
// @filename: f27.ts
export { x } from "./f26";
// @filename: f28.ts
export { x } from "./f27";
// @filename: f29.ts
export { x } from "./f28";
// @filename: f3.ts
export { x } from "./f2";
// @filename: f30.ts
export { x } from "./f29";
// @filename: f31.ts
export { x } from "./f30";
// @filename: f32.ts
export { x } from "./f31";
// @filename: f4.ts
export { x } from "./f3";
// @filename: f5.ts
export { x } from "./f4";
// @filename: f6.ts
export { x } from "./f5";
// @filename: f7.ts
export { x } from "./f6";
// @filename: f8.ts
export { x } from "./f7";
// @filename: f9.ts
export { x } from "./f8";
// @filename: u.ts
import { x as x31 } from "./f31";
import { x as x32 } from "./f32";
const s31: string = x31;
const s32: string = x32;
const sentinel: number = "";
