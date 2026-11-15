#include <SDL2/SDL.h>
#include <proto-include.h>
#include <orbis/Sysmodule.h>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <qrcodegen.hpp>

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
static void rect(int x,int y,int w,int h,int r,int g,int b);

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

static void rect(int x,int y,int w,int h,int r,int g,int b) {
    SDL_SetRenderDrawColor(ren,r,g,b,255);
    SDL_Rect q{x,y,w,h};
    SDL_RenderFillRect(ren,&q);
}

static void drawText(const char* text,int x,int y,int size,int r,int g,int b,bool rtl=false) {
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

static void draw() {
    rect(0,0,1920,1080,49,51,56);
    rect(0,0,82,1080,30,31,34);
    rect(82,0,330,1080,43,45,49);

    // Discord-inspired navigation rail
    rect(17,24,48,48,88,101,242); drawText("D",31,34,30,255,255,255);
    rect(25,91,32,2,79,82,91);
    rect(17,111,48,48,page==1?88:65,page==1?101:67,page==1?242:74);
    drawText("S",31,121,28,255,255,255);
    rect(17,174,48,48,page==0?88:65,page==0?101:67,page==0?242:74);
    drawText("Q",31,184,27,255,255,255);

    drawText("Discord",105,29,29,242,243,245);
    rect(82,78,330,1,37,39,43);
    drawText("NAVIGATION",105,112,18,148,155,164);
    if(page==0) rect(96,145,300,52,64,66,73);
    if(page==1) rect(96,209,300,52,64,66,73);
    drawText("Login",112,157,23,page==0?255:190,page==0?255:192,page==0?255:200);
    drawText("Servers",112,221,23,page==1?255:190,page==1?255:192,page==1?255:200);

    rect(82,990,330,90,35,36,40);
    rect(101,1010,42,42,88,101,242);
    drawText("M",113,1014,27,255,255,255);
    drawText("Discord PS4",385,1009,22,242,243,245);
    drawText(linkState==2?"Connected":"Not connected",105,1038,16,148,155,164);

    rect(412,0,1508,78,49,51,56);
    rect(412,77,1508,1,37,39,43);
    if(page==0) {
        drawText("Discord Login",470,28,28,242,243,245);
        rect(640,145,1000,790,43,45,49);
        drawText("Connect your Discord account",700,190,30,242,243,245);
        drawText("Each user connects their own account",700,248,22,185,187,194);

        if(linkState==1 && !qrUrl.empty()) {
            drawQr(qrUrl,910,330,360);
            drawText("Scan this QR with your phone",700,735,20,196,199,206);
            drawText("Open the page and continue to Discord",700,780,18,196,199,206);
        } else if(linkState==2) {
            rect(910,330,360,360,65,67,74);
            drawText("OK",1055,450,54,255,255,255,false);
            drawText("Connected as",700,735,20,196,199,206);
            drawText(linkedUser.c_str(),700,780,24,255,255,255);
        } else {
            rect(910,330,360,360,255,255,255);
            rect(925,345,330,330,235,237,240);
            rect(1040,455,100,100,65,67,74);
            drawText("LINK",1100,478,36,255,255,255,false);
            drawText(networkMessage.empty()?"Press X to create a real QR code":networkMessage.c_str(),650,735,19,196,199,206);
            drawText("Each person links their own Discord account",700,780,18,148,155,164);
        }
        rect(790,840,700,2,65,67,74);
        drawText("Press RIGHT to open the Servers page",700,875,20,148,155,164);
    } else {
        drawText("Your Discord Servers",470,28,28,242,243,245);
        rect(470,140,1380,820,43,45,49);
        drawText("SERVERS",510,180,24,242,243,245);
        rect(510,225,1300,1,65,67,74);
        rect(1030,355,250,250,56,58,64);
        drawText("D",1172,420,76,255,255,255,false);
        if(linkState==2) {
            drawText("Connected as",700,620,21,185,187,194);
            drawText(linkedUser.c_str(),700,665,28,242,243,245);
            drawText("Servers available: ",700,725,20,185,187,194);
            drawText(std::to_string(linkedGuildCount).c_str(),920,725,20,242,243,245);
        } else {
            drawText("No servers to display yet",700,670,27,242,243,245);
            drawText("Connect your Discord account to load your servers",700,725,20,185,187,194);
        }
        drawText("No demo servers or fabricated data will be shown",700,775,18,148,155,164);
    }
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
                if(e.key.keysym.sym==SDLK_RIGHT) page=1;
                if(e.key.keysym.sym==SDLK_LEFT) page=0;
                if(e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_KP_ENTER) { if(page==0) beginLink(); else page=0; }
                draw();
            }
            if(e.type==SDL_JOYBUTTONDOWN && e.jbutton.button==0) {
                if(page==0) beginLink(); else page=0;
                draw();
            }
            if(e.type==SDL_JOYHATMOTION) {
                if(e.jhat.value&SDL_HAT_RIGHT) page=1;
                if(e.jhat.value&SDL_HAT_LEFT) page=0;
                draw();
            }
        }
        const uint32_t now=SDL_GetTicks();
        if(page==0 && linkState==1 && now-lastPoll>=2000) { pollLink(); lastPoll=now; draw(); }
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
