interface A { url: string }
declare class A { url(): string; }
interface B { get url(): string }
interface B { url: string }
declare class C { get url(): string; }
interface C { url(): string }
