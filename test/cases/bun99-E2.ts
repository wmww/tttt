// @filename: tsconfig.json
{
  "compilerOptions": {
    "strict": true,
    "noEmit": true,
    "skipLibCheck": true,
    "module": "ESNext",
    "moduleResolution": "bundler",
    "types": []
  },
  "include": ["index.ts", "sentinel.ts"]
}
// @filename: index.ts
import type { Fn } from "lib";

export const f: Fn = (method: number) => {};
// @filename: node_modules/lib/index.d.ts
export type Method = typeof Method;
export declare const Method: { a: number };
export type Method = "get" | "post";
export type Fn = (method: Method) => void;
// @filename: node_modules/lib/package.json
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
// @filename: sentinel.ts
import type { Fn, Method } from "lib";
import { Method as Value } from "lib";

export const g: Fn = (method: string) => {};
export const asType: never = null! as Method;
export const asValue: never = Value;
