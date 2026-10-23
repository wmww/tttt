// origin: npm @vobs/theme@1.8.18 https://github.com/vobsjs/vobs tsconfig.bench.json seed 20261029
// flip: no
// JSX.IntrinsicElements an intersection of mapped types (HTML and SVG tag maps): its properties are the tags (TS2339 'div' does not exist in tttt)
// @jsx: preserve
// @lib: es2022, dom
// @filename: jsx.d.ts
export interface A { id?: string }
export interface S { d?: string }
declare global {
  namespace JSX {
    type Element = any
    type IntrinsicElements = {
      [K in keyof HTMLElementTagNameMap]: A
    } & {
      [K in keyof SVGElementTagNameMap]: S
    }
  }
}
// @filename: a.tsx
import './jsx'
const x = <div id="a"></div>
const y = <circle d="x" />
const z = <a id={1} />
const w = <span nope="1" />
