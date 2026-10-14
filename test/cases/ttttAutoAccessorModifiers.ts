// @target: es2022
// An auto-accessor is a property declaration to checkVariableLikeDeclaration: its optionality and modifiers must
// match the interface declarations merged with it (TS2687)
class Foo1 {
    accessor x: string = "abc"
}
interface Foo1 {
    x?: string
}
class Foo2 {
    accessor y: string = "abc"
}
interface Foo2 {
    y: string
}
