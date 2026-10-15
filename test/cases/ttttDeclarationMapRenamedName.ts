// @declaration: true
// @declarationMap: true
// A computed literal name renamed by its text keeps the computed name's range in the map
export class L { m() { return { ['_rolumns']: [] as never[], ['a b']: 1 }; } }
