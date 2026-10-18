// A type tsc makes without a symbol (the decorator context's name/private/static override) prints from its members past
// the truncation length, `{ ...; }`, not `...` (createAnonymousTypeNodeEx; mikro-orm's TS1240 chains)
// @noErrorTruncation: false
// @strict: true
// @target: es2022
declare function Dec(options: string): (target: any, propertyName: string) => void;
class SomeVeryLongClassNameForTheEntityUnderTest {}
interface AnotherQuiteLongInterfaceName<T> { value: T; other: T }
export class AVeryLongOwnerClassNameThatTakesSpace {
  @Dec("x")
  someVeryLongPropertyName?: AnotherQuiteLongInterfaceName<SomeVeryLongClassNameForTheEntityUnderTest> & { id: number; anotherPropertyName: string; yetAnotherPropertyName: string; andOneMore: number; andTheLastOne: boolean };
}
