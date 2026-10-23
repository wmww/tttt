// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: x.ts
import './y';
export class C { private x = 1 }
export const sentinel: number = "";
// @filename: y.ts
import { C } from './x';
new C()["x"];
