// An ES decorator on an optional field gets the field's type with undefined (tsc's getTypeOfNode, the symbol's type):
// ClassFieldDecoratorContext<A, B | undefined> (mikro-orm's legacy decorators used without experimentalDecorators)
// @target: es2022
// @strict: true
declare function Dec(options: string): (target: any, propertyName: string) => void;
declare function Dec2(): (target: any, propertyName: string) => void;
class B {}
export class A {
  @Dec("x")
  b?: B;
  @Dec2()
  c?: B;
  @Dec2()
  d!: B;
}
