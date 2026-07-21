// Headless smoke test: the real engine, loaded in Node, answers a hello.
import assert from "node:assert/strict";
import createEngine from "../web/engine.js";

const Module = await createEngine();
const call = (name, argTypes, args) =>
  Module.ccall(name, "string", argTypes, args);

const drain = () => JSON.parse(call("ha_drain", [], []));
const reset = () => { Module.ccall("ha_reset", null, [], []); drain(); };
const input = (wsId, obj) =>
  Module.ccall("ha_input", null, ["number", "string"], [wsId, JSON.stringify(obj)]);

reset();
input(1, { t: "hello", nick: "tarik", avatar: "🙂" });
const out = drain();

const welcome = out.find((o) => o.to === "ws" && o.msg.t === "welcome");
assert.ok(welcome, "expected a welcome addressed to the joining socket");
assert.equal(welcome.id, 1, "welcome goes to the socket that said hello");
assert.equal(welcome.msg.nick, "TARIK", "engine uppercases nicknames on hello");

const join = out.find((o) => o.to === "uart" && o.kind === "join");
assert.ok(join, "expected a UART join for the Flipper");
assert.equal(join.nick, "TARIK");

console.log("smoke: OK");
