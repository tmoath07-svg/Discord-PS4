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
test("health endpoint reports missing deployment configuration without exposing values",async()=>{const r=await worker.fetch(new Request("https://example.test/api/health"),{LINKS:new MemoryKV()});assert.equal(r.status,200);const x=await r.json();assert.equal(x.ok,true);assert.equal(x.configured,false);assert.ok(x.missing.includes("DISCORD_CLIENT_SECRET"));assert.equal(JSON.stringify(x).includes("test-secret"),false);});
test("start creates a unique session and separate poll secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);assert.equal(r.status,201);const x=await r.json();assert.ok(x.session_id);assert.ok(x.poll_token);assert.ok(x.qr_url.endsWith("/link/"+x.session_id));assert.equal(x.qr_url.includes(x.poll_token),false);});
test("poll rejects missing secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/api/link/status/"+x.session_id),e);assert.equal(p.status,401);});
test("poll accepts correct secret",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/api/link/status/"+x.session_id,{headers:{authorization:"Bearer "+x.poll_token}}),e);assert.equal(p.status,200);assert.equal((await p.json()).status,"pending");});
test("OAuth start redirects to Discord with identify and guilds scopes and PKCE",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/auth/discord?session="+x.session_id),e);assert.equal(p.status,302);const u=new URL(p.headers.get("location"));assert.equal(u.origin,"https://discord.com");assert.equal(u.searchParams.get("scope"),"identify guilds");assert.equal(u.searchParams.get("code_challenge_method"),"S256");assert.ok(u.searchParams.get("state"));});

test("link page declares Arabic direction and points to Discord authorization",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request(x.qr_url),e);assert.equal(p.status,200);const body=await p.text();assert.match(body,/<html lang="ar" dir="rtl">/);assert.match(body,/<a class="button" dir="ltr"/);assert.match(body,/Continue to Discord/);});
test("OAuth callback rejects an unexpected issuer",async()=>{const e=makeEnv();const r=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);const x=await r.json();const p=await worker.fetch(new Request("https://example.test/auth/discord?session="+x.session_id),e);const state=new URL(p.headers.get("location")).searchParams.get("state");const cb=await worker.fetch(new Request("https://example.test/auth/callback?state="+state+"&code=fake"),e);assert.equal(cb.status,400);assert.match(await cb.text(),/issuer did not match Discord/);});

test("refresh decrypts the stored refresh token and refreshes the linked account",async()=>{
  const e=makeEnv();
  const started=await worker.fetch(new Request("https://example.test/api/link/start",{method:"POST"}),e);
  const session=await started.json();
  const auth=await worker.fetch(new Request("https://example.test/auth/discord?session="+session.session_id),e);
  const state=new URL(auth.headers.get("location")).searchParams.get("state");
  const originalFetch=globalThis.fetch;
  let refreshGrantSeen=false;
  globalThis.fetch=async(url,init={})=>{
    const target=String(url);
    if(target.endsWith("/oauth2/token")){
      const form=new URLSearchParams(init.body);
      if(form.get("grant_type")==="authorization_code")
        return Response.json({access_token:"access-one",refresh_token:"refresh-one",scope:"identify guilds"});
      if(form.get("grant_type")==="refresh_token"){
        refreshGrantSeen=true;
        assert.equal(form.get("refresh_token"),"refresh-one");
        return Response.json({access_token:"access-two",refresh_token:"refresh-two",scope:"identify guilds"});
      }
    }
    if(target.endsWith("/users/@me"))
      return Response.json({id:"123",username:"tester",global_name:"Tester",avatar:null});
    if(target.includes("/users/@me/guilds"))
      return Response.json([{id:"456",name:"Test Guild",icon:null,owner:true}]);
    throw new Error("Unexpected fetch: "+target);
  };
  try{
    const callback=await worker.fetch(new Request("https://example.test/auth/callback?state="+state+"&code=authorization-code&iss=https%3A%2F%2Fdiscord.com"),e);
    assert.equal(callback.status,200);
    const linked=await e.LINKS.get("link:"+session.session_id);
    assert.ok(linked);
    const refresh=await worker.fetch(new Request("https://example.test/api/link/refresh/"+session.session_id,{method:"POST",headers:{authorization:"Bearer "+session.poll_token}}),e);
    assert.equal(refresh.status,200);
    const result=await refresh.json();
    assert.equal(result.status,"linked");
    assert.equal(result.user.username,"tester");
    assert.equal(result.guilds[0].name,"Test Guild");
    assert.equal(refreshGrantSeen,true);
  } finally {
    globalThis.fetch=originalFetch;
  }
});
