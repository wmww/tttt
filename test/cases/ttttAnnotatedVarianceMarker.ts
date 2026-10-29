// @strict: true
// An annotated type parameter's variance isn't measured, so its instantiation with a variance marker, met while another
// type's variance is measured, is related by the annotation (here wrongly `out`), not structurally: AnimalContainer is
// covariant, and AnimalContainer<Animal> isn't assignable to AnimalContainer<Dog>

interface Controller<out T> {
    run: (animal: T) => void;
}

interface Animal {
    run(): void;
}

class Dog implements Animal {
    run() {}
    bark() {}
}

interface AnimalContainer<T> {
    controller: Controller<T>;
}

declare let ca: AnimalContainer<Animal>;
declare let cd: AnimalContainer<Dog>;

ca = cd;
cd = ca;
