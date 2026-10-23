// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "experimentalDecorators": true, "emitDecoratorMetadata": true, "isolatedModules": true } }
// @filename: a.ts
import { J6 } from "./t";
declare const dec: any;
export class A4 { @dec p3!: J6 | (never); }
const sentinel: number = "";
// @filename: t.ts
export interface J6 { a: number }
