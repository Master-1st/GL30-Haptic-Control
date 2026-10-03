#include "gl30_type.h"
#include "kk_oled.h"

/* All UI strings are generated/checked UTF-8, with no input text from a server. */
static uint32_t next_codepoint(const char **text) {
    const uint8_t *p=(const uint8_t *)*text;
    uint32_t c=*p++;
    if(c>=0xe0U) { c=((c&15U)<<12)|((p[0]&63U)<<6)|(p[1]&63U); p+=2; }
    else if(c>=0xc0U) { c=((c&31U)<<6)|(p[0]&63U); p++; }
    *text=(const char *)p;
    return c;
}
static const gl30_type_glyph *find_glyph(gl30_type_face face,uint32_t c) {
    int first=gl30_type_starts[face],last=gl30_type_starts[face+1];
    while(first<last) {
        int mid=first+(last-first)/2;
        uint32_t at=gl30_type_glyphs[mid].codepoint;
        if(at<c) first=mid+1;
        else if(at>c) last=mid;
        else return &gl30_type_glyphs[mid];
    }
    return 0;
}
int gl30_type_width(gl30_type_face face,const char *text) {
    int width=0;
    while(*text) {
        const gl30_type_glyph *g=find_glyph(face,next_codepoint(&text));
        if(g) width+=g->advance;
    }
    return width;
}
void gl30_type_draw(gl30_type_face face,int x,int y,const char *text,uint16_t color) {
    OLED_SetColor(color,0);
    while(*text) {
        const gl30_type_glyph *g=find_glyph(face,next_codepoint(&text));
        if(!g) continue;
        const uint8_t *data=gl30_type_pixels+g->offset;
        for(int row=0;row<g->height;row++) {
            int column=0;
            while(column<g->width) {
                uint8_t token=*data++,alpha=(token&15U)*17U;
                int length=(token>>4)+1,px=x+g->left+column,py=y+g->top+row;
                if(alpha==255U) OLED_DrawHLine((int16_t)px,(int16_t)py,(uint16_t)length);
                else if(alpha) for(int n=0;n<length;n++)
                    OLED_BlendPixelRGB565((int16_t)(px+n),(int16_t)py,color,alpha);
                column+=length;
            }
        }
        x+=g->advance;
    }
}
