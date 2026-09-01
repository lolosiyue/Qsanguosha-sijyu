#!/usr/bin/env node
// Compare web Command IDs and REPLY_COMMAND against artifacts/protocol-v2-flow-matrix.json.
// Field names listed on the matrix are existence-only; types are out of scope.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(webRoot, "..");
const matrixPath = path.join(repoRoot, "artifacts", "protocol-v2-flow-matrix.json");
const protocolPath = path.join(webRoot, "src", "protocol.ts");
const repliesPath = path.join(webRoot, "src", "replies.ts");
const srcDir = path.join(webRoot, "src");

const errors = [];

function fail(message) {
  errors.push(message);
}

function read(file) {
  return fs.readFileSync(file, "utf8");
}

function parseCommandMap(source) {
  const block = source.match(/export const Command = \{([\s\S]*?)\} as const/);
  if (!block)
    throw new Error("protocol.ts: Command object not found");
  const map = new Map();
  for (const match of block[1].matchAll(/([A-Z][A-Z0-9_]*)\s*:\s*(\d+)/g))
    map.set(match[1], Number(match[2]));
  return map;
}

function parseReplyCommand(source, commandByName) {
  const block = source.match(/export const REPLY_COMMAND[^=]*= \{([\s\S]*?)\};/);
  if (!block)
    throw new Error("replies.ts: REPLY_COMMAND not found");
  const map = new Map();
  for (const match of block[1].matchAll(/\[Command\.([A-Z][A-Z0-9_]*)\]\s*:\s*Command\.([A-Z][A-Z0-9_]*)/g)) {
    const requestName = match[1];
    const replyName = match[2];
    const requestId = commandByName.get(requestName);
    const replyId = commandByName.get(replyName);
    if (requestId === undefined)
      fail(`REPLY_COMMAND key Command.${requestName} is not in protocol.ts Command`);
    if (replyId === undefined)
      fail(`REPLY_COMMAND value Command.${replyName} is not in protocol.ts Command`);
    if (requestId !== undefined && replyId !== undefined)
      map.set(requestId, replyId);
  }
  return map;
}

function stripCommandName(raw) {
  return raw.startsWith("S_COMMAND_") ? raw.slice("S_COMMAND_".length) : raw;
}

function walkTsFiles(dir, files = []) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      walkTsFiles(full, files);
      continue;
    }
    if (entry.name.endsWith(".ts") && !entry.name.endsWith(".test.ts"))
      files.push(full);
  }
  return files;
}

const matrix = JSON.parse(read(matrixPath));
const flows = Array.isArray(matrix.flows) ? matrix.flows : [];
if (flows.length === 0)
  throw new Error("flow-matrix has no flows");

const webCommands = parseCommandMap(read(protocolPath));
const webIdToName = new Map();
for (const [name, id] of webCommands) {
  const previous = webIdToName.get(id);
  if (previous && previous !== name)
    fail(`protocol.ts Command ID ${id} is both ${previous} and ${name}`);
  webIdToName.set(id, name);
}

const matrixByName = new Map();
const matrixIdToName = new Map();
for (const flow of flows) {
  const name = stripCommandName(String(flow.command ?? ""));
  const id = Number(flow.command_id);
  if (!name || !Number.isInteger(id))
    continue;
  const existingId = matrixByName.get(name);
  if (existingId !== undefined && existingId !== id)
    fail(`flow-matrix ${name} has command_id ${existingId} and ${id}`);
  matrixByName.set(name, id);
  const existingName = matrixIdToName.get(id);
  if (existingName !== undefined && existingName !== name)
    fail(`flow-matrix command_id ${id} is both ${existingName} and ${name}`);
  matrixIdToName.set(id, name);
}

for (const [name, id] of matrixByName) {
  const webId = webCommands.get(name);
  if (webId === undefined)
    fail(`protocol.ts missing Command.${name} (matrix command_id ${id})`);
  else if (webId !== id)
    fail(`Command.${name} is ${webId} in protocol.ts, ${id} in flow-matrix`);
}

for (const [name, id] of webCommands) {
  const matrixId = matrixByName.get(name);
  if (matrixId !== undefined && matrixId !== id)
    fail(`Command.${name} is ${id} in protocol.ts, ${matrixId} in flow-matrix`);
  const matrixName = matrixIdToName.get(id);
  if (matrixName !== undefined && matrixName !== name)
    fail(`protocol.ts Command.${name}=${id} collides with matrix ${matrixName}`);
}

const replyByRequest = parseReplyCommand(read(repliesPath), webCommands);
const seenReplyRequests = new Set();

for (const flow of flows) {
  if (flow.message_type !== "request" || flow.destination !== "client")
    continue;
  const requestId = Number(flow.command_id);
  const replyId = Number(flow.reply_command_id);
  if (!Number.isInteger(requestId) || !Number.isInteger(replyId) || replyId <= 0)
    continue;
  seenReplyRequests.add(requestId);
  const webReply = replyByRequest.get(requestId);
  const requestName = matrixIdToName.get(requestId) ?? String(requestId);
  if (webReply === undefined)
    fail(`replies.ts REPLY_COMMAND missing ${requestName} (${requestId} → ${replyId})`);
  else if (webReply !== replyId)
    fail(`REPLY_COMMAND[${requestName}] is ${webReply}, matrix reply_command_id is ${replyId}`);
}

for (const [requestId, replyId] of replyByRequest) {
  if (!seenReplyRequests.has(requestId)) {
    const name = webIdToName.get(requestId) ?? String(requestId);
    fail(`REPLY_COMMAND has ${name} → ${replyId} but no room→client request in flow-matrix`);
  }
}

const srcText = walkTsFiles(srcDir).map(read).join("\n");
const seenFields = new Set();
for (const flow of flows) {
  // Only fields the compact client must emit. Destination=client notifications
  // list many GUI-only keys the SPA is allowed to ignore.
  if (flow.source !== "client")
    continue;
  if (flow.message_type !== "request" && flow.message_type !== "reply")
    continue;
  const fields = [
    ...(Array.isArray(flow.required_fields) ? flow.required_fields : []),
    ...(Array.isArray(flow.optional_fields) ? flow.optional_fields : [])
  ];
  const flowName = `${flow.message_type} ${stripCommandName(String(flow.command ?? ""))}`;
  for (const field of fields) {
    if (typeof field !== "string" || field === "schema_version")
      continue;
    const key = `${flow.message_type}:${flow.command}:${field}`;
    if (seenFields.has(key))
      continue;
    seenFields.add(key);
    if (!srcText.includes(field))
      fail(`${flowName} field ${field} is listed in flow-matrix but never appears in web/src`);
  }
}

if (errors.length) {
  for (const error of errors)
    console.error(`protocol-sync: ${error}`);
  console.error(`protocol-sync: ${errors.length} mismatch(es)`);
  process.exit(1);
}

console.log(
  `protocol-sync: ok (${matrixByName.size} commands, ${seenReplyRequests.size} reply maps, ${seenFields.size} field names)`
);
