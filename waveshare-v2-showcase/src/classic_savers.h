#pragma once
// Adapted from the user's TFT_eSPI ScreenSavers: Lines, Bubbles, Starfield.
// This version uses LVGL objects, never writes directly to the LCD behind LVGL.
namespace ClassicSavers {
static constexpr int LINES=50, BUBBLES=10, STARS=80;
struct Line {int16_t x1,y1,x2,y2;};
struct Bubble {int16_t x,y,r; int8_t dx,dy,dr; uint32_t color;};
struct Star {float x,y,dx,dy;uint8_t intensity;};
static Line lines[LINES]={},delta={1,1,1,1};
static Bubble bubbles[BUBBLES]={};
static Star stars[STARS]={};
static lv_obj_t *objects[STARS]={};
static lv_point_precise_t linePoints[LINES][2]={};
static int lineIndex=0, rgb[3]={40,40,40},rgbDelta[3]={1,1,1};
static uint32_t frames=0;
static void resetBubble(Bubble &b) {
  b.x=random(4,236);b.y=random(4,316);b.r=1;
  b.dx=random(-3,6);b.dy=random(-3,6);b.dr=random(1,4);
  b.color=lv_color_to_u32(lv_color_hsv_to_rgb(random(0,360),80,100));
}
static void resetStar(Star &s) {
  s.x=120;s.y=160;
  s.dx=float(random(-45,90))/100.0f;
  s.dy=float(random(-45,90))/100.0f;
  s.intensity=0;
}
static void init(lv_obj_t *parent,int mode) {
  for(auto &o:objects)o=nullptr;
  frames=0;
  if(mode==4){
    memset(lines,0,sizeof(lines));delta={1,1,1,1};lineIndex=0;
    for(int i=0;i<LINES;++i){
      objects[i]=lv_line_create(parent);
      linePoints[i][0]={0,0};linePoints[i][1]={0,0};
      lv_line_set_points(objects[i],linePoints[i],2);
      lv_obj_set_style_line_width(objects[i],1,0);
    }
  }else if(mode==5){
    for(int i=0;i<BUBBLES;++i){
      resetBubble(bubbles[i]);
      objects[i]=lv_obj_create(parent);
      lv_obj_set_style_bg_opa(objects[i],LV_OPA_TRANSP,0);
      lv_obj_set_style_border_width(objects[i],1,0);
      lv_obj_set_style_pad_all(objects[i],0,0);
      lv_obj_remove_flag(objects[i],LV_OBJ_FLAG_SCROLLABLE);
    }
  }else if(mode==6){
    for(int i=0;i<STARS;++i){
      resetStar(stars[i]);
      objects[i]=lv_obj_create(parent);
      lv_obj_set_size(objects[i],2,2);
      lv_obj_set_style_radius(objects[i],0,0);
      lv_obj_set_style_border_width(objects[i],0,0);
      lv_obj_set_style_pad_all(objects[i],0,0);
      lv_obj_set_style_bg_color(objects[i],lv_color_white(),0);
      lv_obj_set_style_bg_opa(objects[i],LV_OPA_TRANSP,0);
      lv_obj_remove_flag(objects[i],LV_OBJ_FLAG_SCROLLABLE);
    }
  }
}
static void tick(int mode) {
  ++frames;
  if(mode==4){
    if(frames%25==0){
      int16_t *d=&delta.x1;
      for(int j=0;j<4;++j)d[j]=constrain(d[j]+random(-1,2),-4,4);
      for(int j=0;j<3;++j)rgbDelta[j]=constrain(rgbDelta[j]+random(-1,2),-3,6);
    }
    int next=(lineIndex+1)%LINES;
    Line &n=lines[next], &prev=lines[lineIndex];
    n.x1=constrain(prev.x1+delta.x1,0,239);
    n.y1=constrain(prev.y1+delta.y1,0,319);
    n.x2=constrain(prev.x2+delta.x2,0,239);
    n.y2=constrain(prev.y2+delta.y2,0,319);
    for(int j=0;j<3;++j)rgb[j]=constrain(rgb[j]+rgbDelta[j],1,255);
    linePoints[next][0]={n.x1,n.y1};
    linePoints[next][1]={n.x2,n.y2};
    lv_line_set_points(objects[next],linePoints[next],2);
    lv_obj_set_style_line_color(objects[next],lv_color_make(rgb[0],rgb[1],rgb[2]),0);
    if((n.x1==0&&delta.x1<0)||(n.x1==239&&delta.x1>0))delta.x1=-delta.x1;
    if((n.y1==0&&delta.y1<0)||(n.y1==319&&delta.y1>0))delta.y1=-delta.y1;
    if((n.x2==0&&delta.x2<0)||(n.x2==239&&delta.x2>0))delta.x2=-delta.x2;
    if((n.y2==0&&delta.y2<0)||(n.y2==319&&delta.y2>0))delta.y2=-delta.y2;
    lineIndex=next;
  }else if(mode==5){
    if(frames%7)return;
    for(int i=0;i<BUBBLES;++i){
      Bubble &b=bubbles[i];
      if(b.r>random(14,25))resetBubble(b);
      b.r+=b.dr;b.x=constrain(b.x+b.dx,1,239);b.y=constrain(b.y+b.dy,1,319);
      int diameter=b.r*2;
      lv_obj_set_size(objects[i],diameter,diameter);
      lv_obj_set_pos(objects[i],b.x-b.r,b.y-b.r);
      lv_obj_set_style_radius(objects[i],LV_RADIUS_CIRCLE,0);
      lv_obj_set_style_border_color(objects[i],lv_color_hex(b.color),0);
    }
  }else if(mode==6){
    for(int i=0;i<STARS;++i){
      Star &s=stars[i];
      s.x+=s.dx;s.y+=s.dy;
      if(s.intensity<252)s.intensity+=3;
      if(s.x<0||s.x>=240||s.y<0||s.y>=320)resetStar(s);
      lv_obj_set_pos(objects[i],int(s.x),int(s.y));
      lv_obj_set_style_bg_opa(objects[i],s.intensity,0);
    }
  }
}
} // namespace ClassicSavers
