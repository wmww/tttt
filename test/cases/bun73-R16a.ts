// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type J<T, S> = S & { k: 1 };
namespace N18 { export type J = I037 & { k: 1 }; }
namespace N27 { export type J = I055 & { k: 1 }; }
namespace N36 { export type J = I073 & { k: 1 }; }
namespace N38 { export type J = I077 & { k: 1 }; }
namespace N40 { export type J = I081 & { k: 1 }; }
namespace N61 { export type J = I123 & { k: 1 }; }
namespace N77 { export type J = I155 & { k: 1 }; }
namespace N92 { export type J = I185 & { k: 1 }; }
namespace N105 { export type J = I211 & { k: 1 }; }
namespace N112 { export type J = I225 & { k: 1 }; }
interface I002 { p2: 1 }
interface I008 { p8: 1 }
interface I026 { p26: 1 }
interface I028 { p28: 1 }
interface I037 { p37: 1 }
interface I044 { p44: 1 }
interface I055 { p55: 1 }
interface I073 { p73: 1 }
interface I077 { p77: 1 }
interface I081 { p81: 1 }
interface I098 { p98: 1 }
interface I123 { p123: 1 }
interface I128 { p128: 1 }
interface I155 { p155: 1 }
interface I185 { p185: 1 }
interface I200 { p200: 1 }
interface I204 { p204: 1 }
interface I206 { p206: 1 }
interface I211 { p211: 1 }
interface I214 { p214: 1 }
interface I225 { p225: 1 }
declare const v42: N27.J | N61.J | J<55, I128> | N77.J | J<19, I200> | N40.J | J<12, I214> | N38.J | N18.J | J<105, I028> | N36.J | J<106, I026> | J<16, I206> | J<70, I098> | J<17, I204> | N92.J | J<118, I002> | N105.J | J<115, I008> | J<97, I044> | N112.J;
const sentinel: number = "";
