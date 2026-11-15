#include <SDL2/SDL.h>
#include <proto-include.h>
#include <orbis/Sysmodule.h>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include "qrcodegen.hpp"

std::stringstream debugLogStream;
static SDL_Window* win=nullptr;
static SDL_Renderer* ren=nullptr;
static FT_Library ftLib=nullptr;
static FT_Face arabicFace=nullptr;
static int selected=0;
static int page=0;
#ifndef DISCORD_WORKER_BASE_URL
#define DISCORD_WORKER_BASE_URL "https://discord-ps4-link.example.workers.dev"
#endif
static int netPoolId=-1, sslCtxId=-1, httpCtxId=-1;
static bool networkReady=false;
static int linkState=0;
static std::string sessionId, pollToken, qrUrl, linkedUser, networkMessage;
static int linkedGuildCount=0;
static uint32_t lastPoll=0;
static std::string toastTitle, toastBody;
static uint32_t toastUntil=0;
static void rect(int x,int y,int w,int h,int r,int g,int b);
// Presentation layer only: Discord DM events require the approved Social SDK/RPC notification bridge.
// The current identify+guilds OAuth flow cannot supply private-message events.
static void showToast(const std::string& title,const std::string& body) {
    toastTitle=title; toastBody=body; toastUntil=SDL_GetTicks()+4500;
}

static bool initNetwork() {
    if(static_cast<int32_t>(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET))<0) return false;
    if(static_cast<int32_t>(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL))<0) return false;
    if(static_cast<int32_t>(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP))<0) return false;
    if(sceNetInit()<0) return false;
    netPoolId=sceNetPoolCreate("discord_ps4",16*1024,0);
    if(netPoolId<0) return false;
    sslCtxId=sceSslInit(304*1024);
    if(sslCtxId<0) return false;
    httpCtxId=sceHttpInit(netPoolId,sslCtxId,64*1024);
    if(httpCtxId<0) return false;
    return true;
}

static void shutdownNetwork() {
    if(httpCtxId>=0) { sceHttpTerm(httpCtxId); httpCtxId=-1; }
    if(sslCtxId>=0) { sceSslTerm(); sslCtxId=-1; }
    if(netPoolId>=0) { sceNetPoolDestroy(netPoolId); netPoolId=-1; }
}

static std::string jsonString(const std::string& json,const std::string& key) {
    const std::string marker="\"" + key + "\"";
    size_t p=json.find(marker);
    if(p==std::string::npos) return "";
    p=json.find(':',p+marker.size());
    if(p==std::string::npos) return "";
    p=json.find_first_not_of(" \t\r\n",p+1);
    if(p==std::string::npos || json[p]!='"') return "";
    ++p;
    std::string out;
    while(p<json.size()) {
        char c=json[p++];
        if(c=='"') break;
        if(c=='\\' && p<json.size()) {
            char e=json[p++];
            if(e=='n') out+='\n'; else if(e=='r') out+='\r'; else if(e=='t') out+='\t'; else out+=e;
        } else out+=c;
    }
    return out;
}

static int jsonArrayObjectCount(const std::string& json,const std::string& key) {
    size_t p=json.find("\""+key+"\"");
    if(p==std::string::npos) return 0;
    p=json.find('[',p);
    if(p==std::string::npos) return 0;
    int depth=0,count=0;
    bool quoted=false,escape=false;
    for(++p;p<json.size();++p) {
        char c=json[p];
        if(quoted) { if(escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') quoted=false; continue; }
        if(c=='"') quoted=true;
        else if(c=='{') { if(depth==0) ++count; ++depth; }
        else if(c=='}') { if(depth>0) --depth; }
        else if(c==']' && depth==0) break;
    }
    return count;
}

static int httpRequest(const char* method,const std::string& path,const std::string& body,const std::string& bearer,std::string& response) {
    if(!networkReady) return -1;
    std::string base=DISCORD_WORKER_BASE_URL;
    while(!base.empty() && base.back()=='/') base.pop_back();
    if(base.find(".example.workers.dev")!=std::string::npos) return -2;
    const std::string url=base+path;
    int tpl=sceHttpCreateTemplate(httpCtxId,"Discord-PS4/1.0",ORBIS_HTTP_VERSION_1_1,0);
    if(tpl<0) return tpl;
    sceHttpSetConnectTimeOut(tpl,10000000);
    sceHttpSetSendTimeOut(tpl,10000000);
    int conn=sceHttpCreateConnectionWithURL(tpl,url.c_str(),true);
    if(conn<0) { sceHttpDeleteTemplate(tpl); return conn; }
    const int verb=(strcmp(method,"POST")==0)?ORBIS_METHOD_POST:ORBIS_METHOD_GET;
    int req=sceHttpCreateRequestWithURL(conn,verb,url.c_str(),body.size());
    if(req<0) { sceHttpDeleteConnection(conn); sceHttpDeleteTemplate(tpl); return req; }
    sceHttpAddRequestHeader(req,"Accept","application/json",0);
    if(verb==ORBIS_METHOD_POST) sceHttpAddRequestHeader(req,"Content-Type","application/json",0);
    if(!bearer.empty()) {
        const std::string auth="Bearer "+bearer;
        sceHttpAddRequestHeader(req,"Authorization",auth.c_str(),0);
    }
    int ret=sceHttpSendRequest(req,body.empty()?nullptr:body.data(),body.size());
    int status=0;
    if(ret>=0) ret=sceHttpGetStatusCode(req,&status);
    if(ret>=0) {
        char buffer[2048];
        int bytes=0;
        size_t total=0;
        while((bytes=sceHttpReadData(req,buffer,sizeof(buffer)))>0 && total<65536) {
            response.append(buffer,static_cast<size_t>(bytes));
            total+=static_cast<size_t>(bytes);
        }
        if(bytes<0) ret=bytes;
    }
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return ret<0?ret:status;
}

static bool beginLink() {
    linkState=0; qrUrl.clear(); linkedUser.clear(); linkedGuildCount=0;
    networkMessage.clear();
    showToast("Discord","Starting secure account linking...");
    if(!networkReady) { networkMessage="PS4 network service is unavailable."; return false; }
    if(std::string(DISCORD_WORKER_BASE_URL).find(".example.workers.dev")!=std::string::npos) {
        networkMessage="Worker URL is not configured in the build yet."; return false;
    }
    std::string response;
    int status=httpRequest("POST","/api/link/start","{}","",response);
    if(status!=201) { networkMessage="Could not start a link session. Check the Worker deployment."; return false; }
    sessionId=jsonString(response,"session_id");
    pollToken=jsonString(response,"poll_token");
    qrUrl=jsonString(response,"qr_url");
    if(sessionId.empty()||pollToken.empty()||qrUrl.empty()) {
        networkMessage="The Worker returned an incomplete link session."; return false;
    }
    linkState=1;
    networkMessage="Scan this QR with your phone, then approve on Discord.";
    lastPoll=SDL_GetTicks();
    return true;
}

static void pollLink() {
    if(linkState!=1 || sessionId.empty() || pollToken.empty()) return;
    std::string response;
    const std::string path="/api/link/status/"+sessionId;
    int status=httpRequest("GET",path,"",pollToken,response);
    if(status!=200) return;
    const std::string state=jsonString(response,"status");
    if(state=="linked") {
        linkedUser=jsonString(response,"global_name");
        if(linkedUser.empty()) linkedUser=jsonString(response,"username");
        linkedGuildCount=jsonArrayObjectCount(response,"guilds");
        linkState=2;
        networkMessage="Discord account linked successfully.";
        showToast("Discord connected",linkedUser.empty()?"Your account is now linked.":("Welcome, "+linkedUser));
    } else if(state=="denied" || state=="error" || state=="reauth_required") {
        linkState=3;
        networkMessage="Discord linking did not complete. Start again.";
    }
}

static void drawQr(const std::string& data,int x,int y,int boxSize) {
    const qrcodegen::QrCode qr=qrcodegen::QrCode::encodeText(data.c_str(),qrcodegen::QrCode::Ecc::MEDIUM);
    const int size=qr.getSize(),quiet=4;
    const int scale=boxSize/(size+quiet*2);
    const int actual=scale*(size+quiet*2);
    const int ox=x+(boxSize-actual)/2,oy=y+(boxSize-actual)/2;
    rect(x,y,boxSize,boxSize,255,255,255);
    for(int yy=0;yy<size+quiet*2;++yy) for(int xx=0;xx<size+quiet*2;++xx) {
        const int mx=xx-quiet,my=yy-quiet;
        if(mx>=0&&my>=0&&mx<size&&my<size&&qr.getModule(mx,my))
            rect(ox+xx*scale,oy+yy*scale,scale,scale,0,0,0);
    }
}

struct ArabicForm {
    uint32_t base, isolated, finalForm, initial, medial;
    bool joinsPrevious, joinsNext;
};

static const ArabicForm forms[] = {
    {0x0622,0xFE81,0xFE82,0,0,true,false}, {0x0623,0xFE83,0xFE84,0,0,true,false},
    {0x0624,0xFE85,0xFE86,0,0,true,false}, {0x0625,0xFE87,0xFE88,0,0,true,false},
    {0x0626,0xFE89,0xFE8A,0xFE8B,0xFE8C,true,true}, {0x0627,0xFE8D,0xFE8E,0,0,true,false},
    {0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,true,true}, {0x0629,0xFE93,0xFE94,0,0,true,false},
    {0x062A,0xFE95,0xFE96,0xFE97,0xFE98,true,true}, {0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,true,true},
    {0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,true,true}, {0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,true,true},
    {0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,true,true}, {0x062F,0xFEA9,0xFEAA,0,0,true,false},
    {0x0630,0xFEAB,0xFEAC,0,0,true,false}, {0x0631,0xFEAD,0xFEAE,0,0,true,false},
    {0x0632,0xFEAF,0xFEB0,0,0,true,false}, {0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,true,true},
    {0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,true,true}, {0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,true,true},
    {0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,true,true}, {0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,true,true},
    {0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,true,true}, {0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,true,true},
    {0x063A,0xFECD,0xFECE,0xFECF,0xFED0,true,true}, {0x0641,0xFED1,0xFED2,0xFED3,0xFED4,true,true},
    {0x0642,0xFED5,0xFED6,0xFED7,0xFED8,true,true}, {0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,true,true},
    {0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,true,true}, {0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,true,true},
    {0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,true,true}, {0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,true,true},
    {0x0648,0xFEED,0xFEEE,0,0,true,false}, {0x0649,0xFEEF,0xFEF0,0,0,true,false},
    {0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,true,true}
};

static const ArabicForm* findForm(uint32_t cp) {
    for (const auto& f : forms) if (f.base == cp) return &f;
    return nullptr;
}

static std::vector<uint32_t> decodeUtf8(const char* s) {
    std::vector<uint32_t> out;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        uint32_t cp=0;
        if (*p < 0x80) { cp=*p++; }
        else if ((*p & 0xE0)==0xC0 && p[1]) { cp=((p[0]&0x1F)<<6)|(p[1]&0x3F); p+=2; }
        else if ((*p & 0xF0)==0xE0 && p[1] && p[2]) { cp=((p[0]&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
        else if ((*p & 0xF8)==0xF0 && p[1] && p[2] && p[3]) { cp=((p[0]&7)<<18)|((p[1]&0x3F)<<12)|((p[2]&0x3F)<<6)|(p[3]&0x3F); p+=4; }
        else { cp=0xFFFD; ++p; }
        out.push_back(cp);
    }
    return out;
}

static std::vector<uint32_t> shapeArabic(const char* text) {
    auto cps=decodeUtf8(text);
    for (size_t i=0;i<cps.size();++i) {
        const ArabicForm* cur=findForm(cps[i]);
        if (!cur) continue;
        const ArabicForm* prev=(i>0)?findForm(cps[i-1]):nullptr;
        const ArabicForm* next=(i+1<cps.size())?findForm(cps[i+1]):nullptr;
        bool joinsPrev=prev && prev->joinsNext && cur->joinsPrevious;
        bool joinsNext=next && cur->joinsNext && next->joinsPrevious;
        uint32_t shaped=cur->isolated;
        if (joinsPrev && joinsNext && cur->medial) shaped=cur->medial;
        else if (joinsPrev && cur->finalForm) shaped=cur->finalForm;
        else if (joinsNext && cur->initial) shaped=cur->initial;
        cps[i]=shaped;
    }
    std::vector<uint32_t> visual;
    visual.reserve(cps.size());
    for (auto it=cps.rbegin();it!=cps.rend();++it) visual.push_back(*it);
    return visual;
}

static void drawText(const char* text,int x,int y,int size,int r,int g,int b,bool rtl=false);

static void rect(int x,int y,int w,int h,int r,int g,int b) {
    SDL_SetRenderDrawColor(ren,r,g,b,255);
    SDL_Rect q{x,y,w,h};
    SDL_RenderFillRect(ren,&q);
}

static void rectAlpha(int x,int y,int w,int h,int r,int g,int b,int a) {
    SDL_SetRenderDrawColor(ren,r,g,b,a);
    SDL_Rect q{x,y,w,h};
    SDL_RenderFillRect(ren,&q);
}

static void roundedPanel(int x,int y,int w,int h,int radius,int r,int g,int b,int a) {
    rectAlpha(x+radius,y,w-2*radius,h,r,g,b,a);
    rectAlpha(x,y+radius,radius,h-2*radius,r,g,b,a);
    rectAlpha(x+w-radius,y+radius,radius,h-2*radius,r,g,b,a);
    for(int dy=-radius;dy<=radius;++dy) {
        const int dx=static_cast<int>(std::sqrt(static_cast<double>(radius*radius-dy*dy)));
        SDL_SetRenderDrawColor(ren,r,g,b,a);
        SDL_RenderDrawLine(ren,x+radius-dx,y+radius+dy,x+radius+dx,y+radius+dy);
        SDL_RenderDrawLine(ren,x+w-radius-dx,y+radius+dy,x+w-radius+dx,y+radius+dy);
    }
}

static void drawNotificationToast() {
    if(toastUntil==0) return;
    const uint32_t now=SDL_GetTicks();
    if(static_cast<int32_t>(toastUntil-now)<=0) { toastUntil=0; return; }
    const uint32_t remaining=toastUntil-now;
    const int alpha=remaining<450?static_cast<int>(remaining*215/450):215;
    const int x=1320,y=34,w=560,h=112;
    roundedPanel(x,y,w,h,22,18,20,28,alpha);
    roundedPanel(x+12,y+17,6,h-34,3,88,101,242,alpha);
    drawText(toastTitle.c_str(),x+36,y+19,23,255,255,255,false);
    drawText(toastBody.c_str(),x+36,y+58,17,205,210,220,false);
}

static void drawText(const char* text,int x,int y,int size,int r,int g,int b,bool rtl) {
    if (!arabicFace || !text) return;
    FT_Set_Char_Size(arabicFace,0,size*64,72,72);
    std::vector<uint32_t> cps=rtl?shapeArabic(text):decodeUtf8(text);
    int width=0;
    for (uint32_t cp:cps) {
        FT_UInt gi=FT_Get_Char_Index(arabicFace,cp);
        if (FT_Load_Glyph(arabicFace,gi,0)==0) width+=static_cast<int>(arabicFace->glyph->advance.x>>6);
    }
    int pen=rtl?x-width:x;
    int baseline=y+static_cast<int>(arabicFace->size->metrics.ascender>>6);
    for (uint32_t cp:cps) {
        FT_UInt gi=FT_Get_Char_Index(arabicFace,cp);
        if (FT_Load_Glyph(arabicFace,gi,0)!=0) continue;
        FT_GlyphSlot slot=arabicFace->glyph;
        if (FT_Render_Glyph(slot,ft_render_mode_normal)==0) {
            const FT_Bitmap& bm=slot->bitmap;
            for (unsigned int row=0;row<bm.rows;++row) {
                for (unsigned int col=0;col<bm.width;++col) {
                    int pitch=bm.pitch;
                    const unsigned char* line=(pitch>=0)?bm.buffer+row*pitch:bm.buffer+(bm.rows-1-row)*(-pitch);
                    unsigned char alpha=line[col];
                    if (!alpha) continue;
                    SDL_SetRenderDrawColor(ren,r,g,b,alpha);
                    SDL_RenderDrawPoint(ren,pen+slot->bitmap_left+static_cast<int>(col),
                        baseline-slot->bitmap_top+static_cast<int>(row));
                }
            }
        }
        pen+=static_cast<int>(slot->advance.x>>6);
    }
}

static void drawCircle(int cx,int cy,int radius,int r,int g,int b,int a=255) {
    SDL_SetRenderDrawColor(ren,r,g,b,a);
    for(int dy=-radius;dy<=radius;++dy) {
        const int dx=static_cast<int>(std::sqrt(static_cast<double>(radius*radius-dy*dy)));
        SDL_RenderDrawLine(ren,cx-dx,cy+dy,cx+dx,cy+dy);
    }
}
static void drawNavItem(int y,const char* label,const char* symbol,bool active) {
    if(active) roundedPanel(116,y,258,54,17,75,82,119,210);
    drawCircle(145,y+27,17,active?103:60,active?112:67,active?245:84);
    drawText(symbol,139,y+13,21,255,255,255,false);
    drawText(label,176,y+15,20,active?255:174,active?255:181,active?255:194,false);
}
static void draw() {
    rect(0,0,1920,1080,13,16,27);
    rectAlpha(0,0,1920,240,31,35,63,90);
    roundedPanel(22,22,72,1036,25,20,24,40,240);
    roundedPanel(108,22,286,1036,25,19,23,38,230);
    roundedPanel(410,22,1486,1036,27,18,22,36,225);

    drawCircle(58,67,25,91,101,245);
    drawText("D",49,49,30,255,255,255,false);
    rectAlpha(39,111,38,2,91,97,128,180);
    drawCircle(58,160,20,page==0?100:55,page==0?108:62,page==0?247:77);
    drawText("L",51,146,21,255,255,255,false);
    drawCircle(58,222,20,page==1?100:55,page==1?108:62,page==1?247:77);
    drawText("S",51,208,21,255,255,255,false);
    drawCircle(58,284,20,page==2?100:55,page==2?108:62,page==2?247:77);
    drawText("G",51,270,21,255,255,255,false);

    drawText("DISCORD",132,55,16,143,151,177,false);
    drawText("Your space",132,88,27,245,247,255,false);
    drawNavItem(145,"Account","L",page==0);
    drawNavItem(211,"Servers","S",page==1);
    drawNavItem(277,"Settings","G",page==2);
    rectAlpha(132,958,238,1,95,103,130,100);
    drawCircle(157,1008,19,91,101,245);
    drawText("D",150,995,22,255,255,255,false);
    drawText("Discord PS4",188,989,18,241,243,251,false);
    drawText(linkState==2?"Connected":"Not connected",188,1018,14,linkState==2?126:155,linkState==2?224:164,linkState==2?166:181,false);

    drawText(page==0?"ACCOUNT":(page==1?"SERVERS":"SETTINGS"),458,48,15,145,154,184,false);
    drawText(page==0?"Connect your account":(page==1?"Your communities":"Application preferences"),458,78,27,245,247,255,false);
    roundedPanel(1712,48,148,42,15,31,37,59,220);
    drawCircle(1734,69,5,linkState==2?91:224,linkState==2?215:171,linkState==2?156:91);
    drawText(linkState==2?"ONLINE":"OFFLINE",1750,57,14,203,209,226,false);

    if(page==0) {
        drawText("Welcome to Discord on PlayStation",474,178,34,248,249,255,false);
        drawText("Link your account securely using Discord's official sign-in.",476,231,19,166,176,201,false);
        roundedPanel(474,300,1350,590,28,29,34,57,220);
        roundedPanel(510,338,560,514,22,22,28,47,245);
        drawCircle(790,492,83,87,97,239);
        drawText("D",766,442,96,255,255,255,false);
        drawText("Your account, connected",584,612,25,245,247,255,false);
        drawText("Use the QR code to authorize this PS4 app.",584,659,17,165,176,199,false);
        if(linkState==1 && !qrUrl.empty()) {
            roundedPanel(1190,365,360,360,24,248,249,252,255);
            drawQr(qrUrl,1200,375,340);
            drawText("SCAN TO CONTINUE",1215,754,16,177,187,212,false);
            drawText("Approve the request on Discord.",1175,790,17,174,184,207,false);
        } else if(linkState==2) {
            drawCircle(1370,500,68,52,131,101);
            drawText("OK",1337,470,46,255,255,255,false);
            drawText("Account connected",1190,625,25,242,247,255,false);
            drawText(linkedUser.empty()?"Discord user":linkedUser.c_str(),1190,669,19,170,222,191,false);
            drawText((std::to_string(linkedGuildCount)+" servers available").c_str(),1190,713,17,170,180,204,false);
        } else {
            roundedPanel(1190,365,360,360,24,35,41,66,255);
            drawCircle(1370,510,68,75,85,130);
            drawText("QR",1337,489,38,255,255,255,false);
            drawText("Press X to generate a secure QR code",1125,754,16,182,191,214,false);
            drawText("Your password is entered only on Discord.",1110,790,16,147,159,187,false);
        }
        if(!networkMessage.empty()) drawText(networkMessage.c_str(),515,832,15,226,164,171,false);
        drawText("Press X to link  |  Use LEFT / RIGHT to navigate",515,930,16,133,145,175,false);
    } else if(page==1) {
        drawText("Your servers",474,174,32,246,248,255,false);
        drawText("Communities available to your linked Discord account.",476,222,18,165,176,200,false);
        roundedPanel(474,280,1350,610,26,27,32,53,230);
        if(linkState==2 && linkedGuildCount>0) {
            drawText((std::to_string(linkedGuildCount)+" SERVERS").c_str(),520,318,15,151,161,190,false);
            drawText("Your account is linked. Server names will appear when loaded.",520,380,18,225,230,242,false);
            drawText("Server artwork and channel navigation are the next implementation step.",520,430,16,151,163,190,false);
        } else if(linkState==2) {
            drawCircle(1148,500,57,55,65,91);
            drawText("0",1135,477,35,218,224,239,false);
            drawText("No servers found for this account",1240,475,22,242,245,253,false);
            drawText("Your linked Discord account currently has no accessible servers.",1240,519,16,158,170,195,false);
        } else {
            drawCircle(1148,500,57,55,65,91);
            drawText("S",1134,477,35,218,224,239,false);
            drawText("Connect Discord to see your servers",1240,475,22,242,245,253,false);
            drawText("Only real server data returned by Discord will appear here.",1240,519,16,158,170,195,false);
        }
    } else {
        drawText("Settings",474,174,32,246,248,255,false);
        drawText("Application information and connection status.",476,222,18,165,176,200,false);
        roundedPanel(474,280,1350,610,26,27,32,53,230);
        drawText("GENERAL",520,326,15,147,158,187,false);
        roundedPanel(510,365,1270,104,18,37,43,65,230);
        drawText("Language",544,388,20,243,246,255,false);
        drawText("English (default)",544,424,15,155,167,193,false);
        drawText("Arabic interface is being rebuilt; it is not enabled in this build.",1030,405,15,155,167,193,false);
        roundedPanel(510,493,1270,104,18,37,43,65,230);
        drawText("Discord connection",544,516,20,243,246,255,false);
        drawText(linkState==2?"Your account is linked.":"No Discord account is linked.",544,552,15,155,167,193,false);
        roundedPanel(510,621,1270,104,18,37,43,65,230);
        drawText("Build",544,644,20,243,246,255,false);
        drawText("Discord PS4 | Native interface prototype | v1.10",544,680,15,155,167,193,false);
        drawText("Some Discord features require separate approved platform access.",520,808,15,149,160,188,false);
    }
    drawNotificationToast();
    SDL_RenderPresent(ren);
    SDL_UpdateWindowSurface(win);
}
int main() {
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0) return 1;
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL)!=0) return 2;
    if(FT_Init_FreeType(&ftLib)!=0) return 3;
    if(FT_New_Face(ftLib,"/app0/assets/fonts/NotoSansArabic.ttf",0,&arabicFace)!=0) return 4;
    networkReady=initNetwork();
    if(!networkReady) networkMessage="PS4 network service is unavailable.";
    win=SDL_CreateWindow("Discord PS4",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,1920,1080,SDL_WINDOW_SHOWN);
    if(!win) return 5;
    SDL_ShowWindow(win);
    SDL_Surface* surface=SDL_GetWindowSurface(win);
    if(!surface) return 6;
    ren=SDL_CreateSoftwareRenderer(surface);
    if(!ren) return 7;
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    SDL_Joystick* pad=SDL_NumJoysticks()>0?SDL_JoystickOpen(0):nullptr;
    bool run=true;
    draw();
    while(run) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT) run=false;
            if(e.type==SDL_KEYDOWN) {
                if(e.key.keysym.sym==SDLK_ESCAPE) run=false;
                if(e.key.keysym.sym==SDLK_RIGHT && page<2) ++page;
                if(e.key.keysym.sym==SDLK_LEFT && page>0) --page;
                if(e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_KP_ENTER) { if(page==0) beginLink(); else page=0; }
                draw();
            }
            if(e.type==SDL_JOYBUTTONDOWN && e.jbutton.button==0) {
                if(page==0) beginLink(); else page=0;
                draw();
            }
            if(e.type==SDL_JOYHATMOTION) {
                if(e.jhat.value&SDL_HAT_RIGHT && page<2) ++page;
                if(e.jhat.value&SDL_HAT_LEFT && page>0) --page;
                draw();
            }
        }
        const uint32_t now=SDL_GetTicks();
        if(page==0 && linkState==1 && now-lastPoll>=2000) { pollLink(); lastPoll=now; draw(); }
        if(toastUntil!=0) draw();
        SDL_Delay(10);
    }
    shutdownNetwork();
    if(pad) SDL_JoystickClose(pad);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    if(arabicFace) FT_Done_Face(arabicFace);
    if(ftLib) FT_Done_FreeType(ftLib);
    SDL_Quit();
    return 0;
}
