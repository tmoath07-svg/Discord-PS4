#include <SDL2/SDL.h>
#include <sstream>

// OpenOrbis common logging expects this stream to be defined by the app entry point.
std::stringstream debugLogStream;
static SDL_Window* win; static SDL_Renderer* ren; static int selected=0;
static const char* names[]={"GENERAL","GAMING","MEDIA","VOICE"};
static void box(int x,int y,int w,int h,int r,int g,int b){SDL_SetRenderDrawColor(ren,r,g,b,255);SDL_Rect q{x,y,w,h};SDL_RenderFillRect(ren,&q);}
static const unsigned char* glyph(char c){
 static const unsigned char f[37][5]={
 {62,65,65,65,62},{127,9,9,9,6},{62,65,73,73,50},{65,73,73,73,54},{120,8,8,8,127},{121,73,73,73,79},{62,73,73,73,48},{3,1,113,9,7},{54,73,73,73,54},{6,73,73,73,62},
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{62,81,73,69,62}};
 if(c>='A'&&c<='Z')return f[c-'A']; if(c>='0'&&c<='9')return f[26+c-'0']; static const unsigned char z[5]={0,0,0,0,0};return z;
}
static void label(int x,int y,const char*s,int scale,int r,int g,int b){SDL_SetRenderDrawColor(ren,r,g,b,255);for(;*s;s++,x+=6*scale){auto p=glyph(*s);for(int a=0;a<5;a++)for(int j=0;j<7;j++)if(p[a]&(1<<j)){SDL_Rect q{x+a*scale,y+j*scale,scale,scale};SDL_RenderFillRect(ren,&q);}}}
static void draw(){
 box(0,0,1920,1080,49,51,56);box(0,0,82,1080,30,31,34);box(82,0,330,1080,43,45,49);
 box(17,24,48,48,88,101,242);label(31,42,"D",3,255,255,255);box(25,91,32,2,79,82,91);
 box(17,111,48,48,65,67,74);label(31,128,"G",3,255,255,255);box(17,174,48,48,65,67,74);label(31,191,"F",3,255,255,255);box(17,237,48,48,65,67,74);
 label(112,32,"GAMING HUB",2,242,243,245);box(82,78,330,1,37,39,43);label(108,112,"TEXT CHANNELS",1,148,155,164);
 for(int i=0;i<4;i++){int y=151+i*58;if(i==selected)box(96,y-10,300,46,64,66,73);label(116,y,"#",2,148,155,164);label(145,y+2,names[i],2,i==selected?255:185,i==selected?255:187,i==selected?255:194);}
 label(108,420,"DIRECT MESSAGES",1,148,155,164);label(108,456,"NO CONVERSATIONS YET",1,120,125,134);box(82,990,330,90,35,36,40);box(101,1010,42,42,88,101,242);label(115,1023,"P",3,255,255,255);label(157,1013,"PLAYER",2,242,243,245);label(157,1038,"OFFLINE",1,148,155,164);
 box(412,0,1508,78,49,51,56);box(412,77,1508,1,37,39,43);label(454,29,"#",3,148,155,164);label(492,33,names[selected],2,242,243,245);label(1780,32,"HELP",1,185,187,194);
 box(466,224,70,70,65,67,74);label(488,244,"#",3,215,217,220);label(466,324,"WELCOME TO",2,242,243,245);label(466,356,names[selected],2,242,243,245);label(466,404,"THIS IS THE BEGINNING OF THIS CHANNEL",1,196,199,206);
 box(466,486,1380,1,65,67,74);box(466,516,42,42,88,101,242);label(480,528,"D",3,255,255,255);label(526,518,"DISCORD PS4",2,242,243,245);label(526,550,"WELCOME - OFFLINE INTERFACE PREVIEW",1,196,199,206);
 box(456,930,1408,64,56,58,64);label(484,953,"+",3,196,199,206);label(534,955,"MESSAGE",2,174,178,186);label(900,1033,"OFFLINE PREVIEW",1,133,139,148);SDL_RenderPresent(ren);
    SDL_UpdateWindowSurface(win);
}
int main(){if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0)return 1;win=SDL_CreateWindow("Discord PS4",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,1920,1080,SDL_WINDOW_SHOWN);if(!win)return 2;SDL_ShowWindow(win);SDL_Surface* surface=SDL_GetWindowSurface(win);if(!surface)return 3;ren=SDL_CreateSoftwareRenderer(surface);if(!ren)return 4;SDL_Joystick* pad=SDL_NumJoysticks()>0?SDL_JoystickOpen(0):nullptr;bool run=true;draw();while(run){SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)run=false;if(e.type==SDL_KEYDOWN){if(e.key.keysym.sym==SDLK_ESCAPE)run=false;if(e.key.keysym.sym==SDLK_DOWN)selected=(selected+1)%4;if(e.key.keysym.sym==SDLK_UP)selected=(selected+3)%4;draw();SDL_UpdateWindowSurface(win);}if(e.type==SDL_JOYHATMOTION){if(e.jhat.value&SDL_HAT_DOWN)selected=(selected+1)%4;if(e.jhat.value&SDL_HAT_UP)selected=(selected+3)%4;draw();SDL_UpdateWindowSurface(window);}if(e.type==SDL_JOYBUTTONDOWN&&e.jbutton.button==1)run=false;}}if(pad)SDL_JoystickClose(pad);SDL_DestroyRenderer(ren);SDL_DestroyWindow(win);SDL_Quit();return 0;}
