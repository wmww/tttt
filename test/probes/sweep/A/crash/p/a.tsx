import * as React from "react";
interface Config<Props> { get?: ((props: Props) => void) | undefined }
type PropsWithoutRelay<C extends keyof React.JSX.IntrinsicElements | React.JSXElementConstructor<any>> =
    React.JSX.LibraryManagedAttributes<C, Omit<React.ComponentProps<C>, "relay">>;
declare function create<C extends React.ComponentType<React.ComponentProps<C> & { relay: number }>>(
    Component: C, config: Config<PropsWithoutRelay<C>>): void;
interface Props { relay: number; user: string }
class UserFeed extends React.Component<Props> {}
create(UserFeed, { get(props) {} });
