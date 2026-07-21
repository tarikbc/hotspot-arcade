// Phone panels. Each is the REAL client (web/dist/index.html) in an iframe, wired
// to the in-page engine through the duck-typed socket the client asks us for.
const MAX_PHONES = 8; // AP_MAX_CONN in the firmware

const sockets = new Map(); // wsId -> socket object handed to a client

export function makeSocket(wsId, onSend) {
  const sock = {
    readyState: 1,
    onopen: null,
    onmessage: null,
    onclose: null,
    send(data) { onSend(wsId, data); },
    close() {
      sock.readyState = 3;
      if (sock.onclose) sock.onclose({});
    },
  };
  sockets.set(wsId, sock);
  // The client assigns its handlers immediately after connect() returns, so defer
  // the open until it has had the chance.
  setTimeout(() => { if (sock.onopen) sock.onopen({}); }, 0);
  return sock;
}

export function deliver(wsId, msg) {
  const sock = sockets.get(wsId);
  if (sock && sock.onmessage) sock.onmessage({ data: msg });
}

export function broadcast(msg) {
  for (const wsId of sockets.keys()) deliver(wsId, msg);
}

export function dropSocket(wsId) { sockets.delete(wsId); }
export { MAX_PHONES };
