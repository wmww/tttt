type Constructor = new (...args: any[]) => any;
type RemoveIndex<T> = {
    [P in keyof T as string extends P ? never : number extends P ? never : P]: T[P];
};
type ConstructorProperties<T> = {
    [K in keyof RemoveIndex<T> as RemoveIndex<T>[K] extends Constructor ? K : never]: RemoveIndex<T>[K];
};
type ConstructorPropertyNames<T> = RemoveIndex<keyof ConstructorProperties<T>>;
declare function spyOn<T extends {}, M extends ConstructorPropertyNames<Required<T>>>(
    object: T, method: M): ConstructorProperties<Required<T>>[M];
