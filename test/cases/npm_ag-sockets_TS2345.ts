// origin: npm ag-sockets@14.0.4 https://github.com/Agamnentzar/ag-sockets 0122-Agamnentzar-ag-sockets/tsconfig.bench.json seed 20261040
// flip: no
// @strict: true
// @target: es6
// @module: commonjs
// @filename: node_modules/ws/index.d.ts
declare class WebSocket {
    on(event: "close", listener: (this: WebSocket, code: number) => void): this;
    on(event: string | symbol, listener: (this: WebSocket, ...args: any[]) => void): this;
}
declare namespace WebSocket {
    class Server<T extends WebSocket = WebSocket> {
        handleUpgrade(callback: (client: T) => void): void;
    }
}
export = WebSocket;
// @filename: src/serverSocket.ts
import * as ws from 'ws';
declare const wsLibrary: typeof ws;
const wsServer = new wsLibrary.Server();
wsServer.handleUpgrade(socket => connectSocket(socket));
function connectSocket(socket: ws) {
}
