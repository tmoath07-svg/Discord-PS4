#include <SDL2/SDL.h>
#include <proto-include.h>
#include <orbis/Sysmodule.h>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h>

std::stringstream debugLogStream;
static SDL_Window* win=nullptr;
static SDL_Renderer* ren=nullptr;
static FT_Library ftLib=nullptr;
static FT_Face arabicFace=nullptr;
static hb_blob_t* arabicBlob=nullptr;
static hb_face_t* arabicHbFace=nullptr;
static hb_font_t* arabicHbFont=nullptr;
static int selected=1;

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

static void rect(int x,int y,int w,int h,int r,int g,int b) {
    SDL_SetRenderDrawColor(ren,r,g,b,255);
    SDL_Rect q{x,y,w,h};
    SDL_RenderFillRect(ren,&q);
}

static void drawText(const char* text,int x,int y,int size,int r,int g,int b,bool rtl=false) {
    if (!arabicFace || !text) return;
    FT_Set_Char_Size(arabicFace,0,size*64,72,72);
    const int baseline=y+static_cast<int>(arabicFace->size->metrics.ascender>>6);

    if (rtl && arabicHbFont) {
        hb_font_set_scale(arabicHbFont,size*64,size*64);
        hb_buffer_t* buffer=hb_buffer_create();
        hb_buffer_add_utf8(buffer,text,-1,0,-1);
        hb_buffer_set_direction(buffer,HB_DIRECTION_RTL);
        hb_buffer_set_script(buffer,HB_SCRIPT_ARABIC);
        hb_buffer_set_language(buffer,hb_language_from_string("ar",-1));
        hb_shape(arabicHbFont,buffer,nullptr,0);

        unsigned int count=0;
        hb_glyph_info_t* infos=hb_buffer_get_glyph_infos(buffer,&count);
        hb_glyph_position_t* positions=hb_buffer_get_glyph_positions(buffer,&count);
        int penX=x;
        for (unsigned int i=0;i<count;++i) {
            if (FT_Load_Glyph(arabicFace,infos[i].codepoint,FT_LOAD_DEFAULT)!=0) {
                penX+=positions[i].x_advance/64;
                continue;
            }
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
                        SDL_RenderDrawPoint(ren,
                            penX+positions[i].x_offset/64+slot->bitmap_left+static_cast<int>(col),
                            baseline-positions[i].y_offset/64-slot->bitmap_top+static_cast<int>(row));
                    }
                }
            }
            penX+=positions[i].x_advance/64;
        }
        hb_buffer_destroy(buffer);
        return;
    }

    auto cps=decodeUtf8(text);
    int pen=x;
    for (uint32_t cp:cps) {
        FT_UInt gi=FT_Get_Char_Index(arabicFace,cp);
        if (FT_Load_Glyph(arabicFace,gi,FT_LOAD_DEFAULT)!=0) continue;
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
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL)!=0) return 2;
    if(FT_Init_FreeType(&ftLib)!=0) return 3;
    if(FT_New_Face(ftLib,"/app0/assets/fonts/NotoSansArabic.ttf",0,&arabicFace)!=0) return 4;
    arabicBlob=hb_blob_create_from_file("/app0/assets/fonts/NotoSansArabic.ttf");
    if(!arabicBlob) return 8;
    arabicHbFace=hb_face_create(arabicBlob,0);
    if(!arabicHbFace) return 9;
    arabicHbFont=hb_font_create(arabicHbFace);
    if(!arabicHbFont) return 10;
    hb_ot_font_set_funcs(arabicHbFont);
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
    if(arabicHbFont) hb_font_destroy(arabicHbFont);
    if(arabicHbFace) hb_face_destroy(arabicHbFace);
    if(arabicBlob) hb_blob_destroy(arabicBlob);
    if(arabicFace) FT_Done_Face(arabicFace);
    if(ftLib) FT_Done_FreeType(ftLib);
    SDL_Quit();
    return 0;
}
