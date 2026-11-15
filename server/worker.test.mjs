import test from "node:test";
import assert from "node:assert/strict";
import worker from "./worker.js";
class MemoryKV {
  data = new Map();
  async get(k) { return this.data.get(k) ?? null; }
  async put(k,v,options={}) { if (options.expirationTtl !== undefined) assert.ok(options.expirationTtl >= 60); this.data.set(k,v); }
  async delete(k) { this.data.delete(k); }
}
const makeEnv=()=>({LINKS:new MemoryKV(),DISCORD_CLIENT_ID:"test-client",DISCORD_CLIENT_SECRET:"test-secret",DISCORD_REDIRECT_URI:"https://example.test/auth/callback",TOKEN_ENCRYPTION_KEY:"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"});
test("health endpoint works",async()=>{const r=await worker.fetch(new Request("https://example.test/api/health"),{LINKS:new MemoryKV()});assert.equal(r.status,200);assert.equal((await r.json()).ok,true);});
test("start creates a unique session and separate poll secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);assert.equal(r.status,201);const x=await r.json();assert.ok(x.session_id);assert.ok(x.poll_token);assert.ok(x.qr_url.endsWith("/link/"+x.session_id));assert.equal(x.qr_url.includes(x.poll_token),false);});
test("poll rejects missing secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/api/link/status/"+x.session_id),e);assert.equal(p.status,401);});
test("poll accepts correct secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/api/link/status/"+x.session_id,{headers:{authorization:"Bearer "+x.poll_token}}),e);assert.equal(p.status,200);assert.equal((await p.json()).status,"pending");});
test("OAuth start redirects to Discord with identify and guilds scopes and PKCE",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/auth/discord?session="+x.session_id),e);assert.equal(p.status,302);const u=new URL(p.headers.get("location"));assert.equal(u.origin,"https://discord.com");assert.equal(u.searchParams.get("scope"),"identify guilds");assert.equal(u.searchParams.get("code_challenge_method"),"S256");assert.ok(u.searchParams.get("state"));});
