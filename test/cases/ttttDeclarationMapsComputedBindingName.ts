// @declaration: true
// @declarationMap: true
// @strict: true
// @target: es2022
// @module: esnext
// A binding pattern's computed property name maps its expression too (lit-html's Template constructor)

// @filename: a.ts
interface R { strings: string[]; _$litType$: number; [k: string]: unknown }
const k = "x";
export class Template {
  constructor(
    // A comment
    {strings, ['_$litType$']: type}: R,
    options?: number
  ) {}
}
export function f({ [k]: x, [0]: y }: R) {}
