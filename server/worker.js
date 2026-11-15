const API="https://discord.com/api/v10";
const AUTHORIZE="https://discord.com/oauth2/authorize";
const TOKEN=API+"/oauth2/token";
const LINK_TTL=600, LINKED_TTL=2592000, enc=new TextEncoder();
const json=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{"content-type":"application/json; charset=utf-8","cache-control":"no-store","x-content-type-options":"nosniff"}});
const b64=a=>{let s="";for(const n of a)s+=String.fromCharCode(n);return btoa(s).replace(/\+/g,"-").replace(/\//g,"_").replace(/=+$/g,"");};
const un64=s=>{s=s.replace(/-/g,"+").replace(/_/g,"/");const x=atob(s+"=".repeat((4-s.length%4)%4));return Uint8Array.from(x,c=>c.charCodeAt(0));};
const random=n=>{const a=new Uint8Array(n);crypto.getRandomValues(a);return b64(a);};
const hash=async s=>new Uint8Array(await crypto.subtle.digest("SHA-256",enc.encode(s)));
const same=(a,b)=>{if(a.length!==b.length)return false;let x=0;for(let i=0;i<a.length;i++)x|=a[i]^b[i];return x===0;};
const read=async(kv,k)=>{const s=await kv.get(k);if(!s)return null;try{return JSON.parse(s);}catch{return null;}};
const missing=env=>["LINKS","DISCORD_CLIENT_ID","DISCORD_CLIENT_SECRET","DISCORD_REDIRECT_URI","TOKEN_ENCRYPTION_KEY"].filter(k=>!env[k]);
const bearer=req=>{const s=req.headers.get("authorization")||"";return s.startsWith("Bearer ")?s.slice(7):"";};
const getSession=async(env,id)=>/^[A-Za-z0-9_-]{20,40}$/.test(id)?read(env.LINKS,"link:"+id):null;
const page=(title,message,href)=>'<!doctype html><html lang="ar" dir="rtl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+title+'</title><style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#1e1f22;color:#f2f3f5;font:17px system-ui;padding:22px}.card{width:min(100%,480px);background:#2b2d31;border-radius:18px;padding:30px}h1{font-size:25px}p{color:#c4c7ce;line-height:1.8}.button{display:block;text-align:center;text-decoration:none;background:#5865f2;color:white;padding:14px;border-radius:10px;font-weight:700;margin-top:25px}.muted{color:#949ba4;font-size:13px}</style><main class="card"><h1>'+title+'</h1><p>'+message+'</p>'+(href?'<a class="button" href="'+href+'">المتابعة إلى Discord</a>':'<p class="muted">يمكنك إغلاق هذه الصفحة.</p>')+'<p class="muted">لا تشارك رموز الربط مع أي شخص. الرمز صالح لهذه الجلسة فقط.</p></main></html>';
const html=(body,status=200)=>new Response(body,{status,headers:{"content-type":"text/html; charset=utf-8","cache-control":"no-store","x-content-type-options":"nosniff","content-security-policy":"default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'"}});
async function start(req,env){
 if(req.method!=="POST")return json({error:"method_not_allowed"},405);
 const miss=missing(env);if(miss.length)return json({error:"server_not_configured",missing:miss},503);
 const ip=req.headers.get("CF-Connecting-IP")||"unknown",rk="rate:"+b64(await hash(ip));
 if(await env.LINKS.get(rk))return json({error:"rate_limited"},429);
 await env.LINKS.put(rk,"1",{expirationTtl:60});
 const id=random(18),poll=random(32),expires=Date.now()+LINK_TTL*1000;
 await env.LINKS.put("link:"+id,JSON.stringify({id,pollHash:b64(await hash(poll)),status:"pending",createdAt:Date.now(),expiresAt:expires}),{expirationTtl:LINK_TTL});
 const origin=new URL(req.url).origin;
 return json({session_id:id,poll_token:poll,qr_url:origin+"/link/"+id,expires_at:new Date(expires).toISOString(),poll_interval_seconds:2},201);
}
async function linkPage(id,env,req){
 const s=await getSession(env,id);
 if(!s||s.status!=="pending"||s.expiresAt<Date.now())return html(page("انتهت جلسة الربط","ارجع إلى تطبيق PS4 واطلب رمز QR جديدًا."),410);
 const u=new URL("/auth/discord",req.url);u.searchParams.set("session",id);
 return html(page("ربط حساب Discord","سجّل الدخول في Discord الرسمي ووافق على الصلاحيات المطلوبة. لن نطلب كلمة مرورك داخل هذه الصفحة.",u.toString()));
}
async function begin(req,env,id){
 const s=await getSession(env,id);
 if(!s||s.status!=="pending"||s.expiresAt<Date.now())return html(page("انتهت جلسة الربط","ارجع إلى PS4 وابدأ جلسة جديدة."),410);
 const state=random(32),verifier=random(48),challenge=b64(await crypto.subtle.digest("SHA-256",enc.encode(verifier)));
 await env.LINKS.put("oauth:"+state,JSON.stringify({sessionId:id,verifier}),{expirationTtl:LINK_TTL});
 const u=new URL(AUTHORIZE);
 u.searchParams.set("client_id",env.DISCORD_CLIENT_ID);u.searchParams.set("redirect_uri",env.DISCORD_REDIRECT_URI);
 u.searchParams.set("response_type","code");u.searchParams.set("scope","identify guilds");
 u.searchParams.set("state",state);u.searchParams.set("code_challenge",challenge);u.searchParams.set("code_challenge_method","S256");
 return Response.redirect(u.toString(),302);
}
async function seal(value,keyText){
 const raw=un64(keyText);if(raw.length!==32)throw new Error("TOKEN_ENCRYPTION_KEY must be 32 bytes");
 const key=await crypto.subtle.importKey("raw",raw,"AES-GCM",false,["encrypt"]),iv=crypto.getRandomValues(new Uint8Array(12));
 const data=await crypto.subtle.encrypt({name:"AES-GCM",iv},key,enc.encode(value));return {iv:b64(iv),data:b64(new Uint8Array(data))};
}
async function callback(req,env){
 const u=new URL(req.url),state=u.searchParams.get("state")||"",st=await read(env.LINKS,"oauth:"+state);
 if(!state||!st)return html(page("تعذر إكمال الربط","انتهت صلاحية الطلب. ارجع إلى PS4 وابدأ جلسة جديدة."),400);
 await env.LINKS.delete("oauth:"+state);
 const s=await getSession(env,st.sessionId);
 if(!s||s.status!=="pending"||s.expiresAt<Date.now())return html(page("انتهت جلسة الربط","ارجع إلى PS4 وابدأ جلسة جديدة."),410);
 if(u.searchParams.has("error")){s.status="denied";await env.LINKS.put("link:"+s.id,JSON.stringify(s),{expirationTtl:300});return html(page("لم تتم الموافقة","لم يتم ربط الحساب. ارجع إلى PS4 وحاول مرة أخرى."),400);}
 const code=u.searchParams.get("code");if(!code)return html(page("تعذر إكمال الربط","لم يصل رمز التفويض من Discord."),400);
 const form=new URLSearchParams({client_id:env.DISCORD_CLIENT_ID,client_secret:env.DISCORD_CLIENT_SECRET,grant_type:"authorization_code",code,redirect_uri:env.DISCORD_REDIRECT_URI,code_verifier:st.verifier});
 const tr=await fetch(TOKEN,{method:"POST",headers:{"content-type":"application/x-www-form-urlencoded"},body:form});
 const token=await tr.json();if(!tr.ok||!token.access_token||!token.refresh_token){s.status="error";s.error="token_exchange_failed";await env.LINKS.put("link:"+s.id,JSON.stringify(s),{expirationTtl:300});return html(page("فشل ربط Discord","تعذر إكمال التفويض. راجع إعدادات تطبيق Discord وحاول مجددًا."),502);}
 const headers={authorization:"Bearer "+token.access_token};
 const ur=await fetch(API+"/users/@me",{headers});if(!ur.ok)return fail(env,s,"profile_fetch_failed");
 const user=await ur.json(),gr=await fetch(API+"/users/@me/guilds?limit=200",{headers});
 if(!gr.ok)return fail(env,s,"guild_fetch_failed");const guilds=await gr.json();if(!Array.isArray(guilds))return fail(env,s,"guild_response_invalid");
 s.status="linked";s.linkedAt=Date.now();s.user={id:user.id,username:user.username,global_name:user.global_name||null,avatar:user.avatar||null};
 s.guilds=guilds.map(g=>({id:g.id,name:g.name,icon:g.icon||null,owner:!!g.owner}));
 s.refresh_token=await seal(token.refresh_token,env.TOKEN_ENCRYPTION_KEY);s.scopes=token.scope||"identify guilds";
 await env.LINKS.put("link:"+s.id,JSON.stringify(s),{expirationTtl:LINKED_TTL});
 return html(page("تم ربط الحساب بنجاح","تم ربط حساب Discord بجهاز PS4. يمكنك إغلاق هذه الصفحة والرجوع إلى التطبيق."));
}
async function fail(env,s,reason){s.status="error";s.error=reason;await env.LINKS.put("link:"+s.id,JSON.stringify(s),{expirationTtl:300});return html(page("تعذر جلب بيانات الحساب","لم يتمكن التطبيق من جلب بيانات الحساب والسيرفرات المسموح بها. حاول الربط من جديد."),502);}
async function status(req,env,id){
 if(req.method!=="GET")return json({error:"method_not_allowed"},405);
 const s=await getSession(env,id);if(!s)return json({error:"session_not_found_or_expired"},404);
 const token=bearer(req);if(!token||!same(await hash(token),un64(s.pollHash)))return json({error:"unauthorized"},401);
 if(s.status==="linked")return json({status:"linked",user:s.user,guilds:s.guilds||[],linked_at:new Date(s.linkedAt).toISOString()});
 return json({status:s.status,expires_at:new Date(s.expiresAt).toISOString()});
}
export default {async fetch(req,env){
 const u=new URL(req.url);
 if(req.method==="OPTIONS")return new Response(null,{status:204,headers:{"access-control-allow-origin":"*","access-control-allow-methods":"GET,POST,OPTIONS","access-control-allow-headers":"authorization,content-type","access-control-max-age":"86400"}});
 try{
  if(u.pathname==="/api/health")return json({ok:true,service:"discord-ps4-link"});
  if(u.pathname==="/api/link/start"){const r=await start(req,env);r.headers.set("access-control-allow-origin","*");return r;}
  const sm=u.pathname.match(/^\/api\/link\/status\/([A-Za-z0-9_-]+)$/);
  if(sm){const r=await status(req,env,sm[1]);r.headers.set("access-control-allow-origin","*");return r;}
  const lm=u.pathname.match(/^\/link\/([A-Za-z0-9_-]+)$/);
  if(lm&&req.method==="GET")return linkPage(lm[1],env,req);
  if(u.pathname==="/auth/discord"&&req.method==="GET"){if(missing(env).length)return json({error:"server_not_configured"},503);return begin(req,env,u.searchParams.get("session")||"");}
  if(u.pathname==="/auth/callback"&&req.method==="GET")return callback(req,env);
  return json({error:"not_found"},404);
 }catch{return json({error:"internal_error"},500);}
}};
