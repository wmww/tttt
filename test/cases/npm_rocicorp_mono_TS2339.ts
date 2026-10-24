// origin: npm rocicorp/mono@main https://github.com/rocicorp/mono packages/zero-cache/tsconfig.json seed 20261401
// flip: yes
// @strict: true
// @noEmit: true
// A const array destructuring of a tuple union, `const [tag] = change`, narrows `change` by the destructured
// discriminant in tsgo (switch (tag) / case 'begin' leaves the 'begin' tuple). tttt narrows only `tag`, so
// `change[2]` stays the union of every third element (42 errors on zero-cache: TS2339, TS2532, TS2345...).
type Change =
  | ['begin', { tag: 'begin' }, { commitWatermark: string }]
  | ['data', { tag: 'insert' }]
  | ['commit', { tag: 'commit' }, { watermark: string }];
declare const changes: Change[];
export function f() {
  for (const change of changes) {
    const [tag] = change;
    switch (tag) {
      case 'begin': {
        const { commitWatermark } = change[2];
        return commitWatermark;
      }
      case 'commit':
        return change[2].watermark;
    }
  }
}
