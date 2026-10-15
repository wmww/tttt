// A readonly field initialized with an enum member is written with it (`readonly id = Id.A`), which names the enum:
// tracked like a const variable's (createLiteralConstValue), so its import is kept in a class that only painting
// emits (vscode's inlineCompletionsAccessibleView.d.ts lost `AccessibleViewProviderId`)
// @declaration: true
// @filename: /e.ts
export const enum Id { A = "a", B = "b" }
// @filename: /c.ts
import { Id } from "./e";
class P { public readonly id = Id.A; }
export class C { get() { return new P(); } }
