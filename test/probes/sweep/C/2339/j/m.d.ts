export class Base { set(): void; }
interface Image extends Base {}
export class Image { src: string; }
export as namespace F;
