// @declaration: true
// @declarationMap: true
// The parentheses tsgo's printer adds around a conditional type in an `extends` clause are outside its mappings
export type exactEquals<l, r> = (<_>() => _ extends l ? 1 : 2) extends <_>() => _ extends r ? 1 : 2 ? true : false;
