#include <SDL2/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <orbis/Sysmodule.h>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

std::stringstream debugLogStream;
static SDL_Window* win=nullptr;
static SDL_Renderer* ren=nullptr;
static FT_Library ftLib=nullptr;
static FT_Face arabicFace=nullptr;
static int selected=1;

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
    FT_Set_Pixel_Sizes(arabicFace,0,size);
    std::vector<uint32_t> cps=rtl?shapeArabic(text):decodeUtf8(text);
    int width=0;
    for (uint32_t cp:cps) {
        FT_UInt gi=FT_Get_Char_Index(arabicFace,cp);
        if (FT_Load_Glyph(arabicFace,gi,FT_LOAD_DEFAULT)==0) width+=static_cast<int>(arabicFace->glyph->advance.x>>6);
    }
    int pen=rtl?x-width:x;
    int baseline=y+static_cast<int>(arabicFace->size->metrics.ascender>>6);
    for (uint32_t cp:cps) {
        FT_UInt gi=FT_Get_Char_Index(arabicFace,cp);
        if (FT_Load_Glyph(arabicFace,gi,FT_LOAD_DEFAULT)!=0) continue;
        FT_GlyphSlot slot=arabicFace->glyph;
        if (FT_Render_Glyph(slot,FT_RENDER_MODE_NORMAL)==0) {
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
    const char* names[]={"عام","ألعاب","وسائط","صوت"};
    rect(0,0,1920,1080,49,51,56);
    rect(0,0,82,1080,30,31,34);
    rect(82,0,330,1080,43,45,49);
    rect(17,24,48,48,88,101,242); drawText("D",31,34,30,255,255,255);
    rect(25,91,32,2,79,82,91);
    rect(17,111,48,48,65,67,74); drawText("G",31,121,30,255,255,255);
    rect(17,174,48,48,65,67,74); drawText("F",31,184,30,255,255,255);
    rect(17,237,48,48,65,67,74); drawText("M",27,247,26,255,255,255);
    drawText("ديسكورد",385,29,29,242,243,245,true);
    rect(82,78,330,1,37,39,43);
    drawText("القنوات النصية",385,112,23,148,155,164,true);
    for(int i=0;i<4;i++) {
        int yy=151+i*58;
        if(i==selected) rect(96,yy-10,300,46,64,66,73);
        drawText(names[i],375,yy,25,i==selected?255:185,i==selected?255:187,i==selected?255:194,true);
    }
    drawText("الرسائل المباشرة",385,420,22,148,155,164,true);
    drawText("لا توجد محادثات",385,456,20,120,125,134,true);
    rect(82,990,330,90,35,36,40);
    rect(101,1010,42,42,88,101,242);
    drawText("م",113,1014,27,255,255,255,true);
    drawText("اللاعب",385,1009,24,242,243,245,true);
    drawText("غير متصل",385,1038,18,148,155,164,true);

    rect(412,0,1508,78,49,51,56);
    rect(412,77,1508,1,37,39,43);
    drawText(names[selected],620,28,27,242,243,245,true);
    drawText("مساعدة",1860,30,20,185,187,194,true);

    rect(466,224,70,70,65,67,74);
    drawText("#",488,236,36,215,217,220);
    drawText("مرحباً بك في",1835,324,36,242,243,245,true);
    drawText(names[selected],1835,375,34,242,243,245,true);
    drawText("هذه بداية القناة، ابدأ المحادثة هنا",1835,430,23,196,199,206,true);

    rect(466,486,1380,1,65,67,74);
    rect(466,516,42,42,88,101,242);
    drawText("D",480,520,27,255,255,255);
    drawText("DISCORD PS4",526,516,27,242,243,245);
    drawText("واجهة تجريبية - بدون اتصال",526,549,20,196,199,206,true);

    rect(456,930,1408,64,56,58,64);
    drawText("+",484,941,39,196,199,206);
    drawText("اكتب رسالة...",1830,950,25,174,178,186,true);
    drawText("معاينة بدون اتصال",1860,1033,18,133,139,148,true);
    SDL_RenderPresent(ren);
    SDL_UpdateWindowSurface(win);
}

int main() {
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0) return 1;
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE)!=0) return 2;
    if(FT_Init_FreeType(&ftLib)!=0) return 3;
    if(FT_New_Face(ftLib,"/app0/assets/fonts/NotoSansArabic.ttf",0,&arabicFace)!=0) return 4;
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
                if(e.key.keysym.sym==SDLK_DOWN) selected=(selected+1)%4;
                if(e.key.keysym.sym==SDLK_UP) selected=(selected+3)%4;
                draw();
            }
            if(e.type==SDL_JOYHATMOTION) {
                if(e.jhat.value&SDL_HAT_DOWN) selected=(selected+1)%4;
                if(e.jhat.value&SDL_HAT_UP) selected=(selected+3)%4;
                draw();
            }
        }
    }
    if(pad) SDL_JoystickClose(pad);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    if(arabicFace) FT_Done_Face(arabicFace);
    if(ftLib) FT_Done_FreeType(ftLib);
    SDL_Quit();
    return 0;
}
