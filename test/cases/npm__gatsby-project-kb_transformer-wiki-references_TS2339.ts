// origin: npm @gatsby-project-kb/transformer-wiki-references@0.3.1 https://github.com/hikerpig/gatsby-project-kb tsconfig.json seed 20261206
// flip: no
// @strict: true
// @noEmit: true
// @lib: dom, esnext
// An aliased condition (`const isClient = typeof window === 'object'`) narrows only const variables, readonly
// properties and unassigned parameters (tsgo's isConstantReference). `window`/`w` are `var`s, so tsgo leaves them
// alone after `if (isClient) return`. tttt narrows them to never (spurious TS2339).
declare var w: { a(): void };
export function f() {
  const ok = typeof w === 'object'
  if (ok) {
    return
  }
  w.a()
}
export function useWindowSize() {
  const isClient = typeof window === 'object'
  return () => {
    if (isClient) {
      return
    }
    window.addEventListener('resize', () => {})
  }
}
