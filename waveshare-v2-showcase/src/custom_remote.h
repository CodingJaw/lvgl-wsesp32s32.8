#pragma once
#include <Preferences.h>
#include <WebServer.h>
#include <lvgl.h>
// Standalone, web-defined remote page. Buttons intentionally emit events only;
// no IR/USB/network command is executed until command routing is configured.
namespace CustomRemote {
static Preferences prefs;
static bool enabled=false,changed=false;
static uint8_t columns=2,count=0;
static uint32_t background=0x14213D,buttonColor=0x31568A;
static String title="My Remote";
static String labels[12];
static void begin(){
  prefs.begin("custom-ui",false);
  enabled=prefs.getBool("enabled",false);
  columns=prefs.getUChar("cols",2);
  if(columns<2||columns>3)columns=2;
  count=prefs.getUChar("count",0);
  if(count>12)count=12;
  background=prefs.getUInt("bg",0x14213D)&0xFFFFFF;
  buttonColor=prefs.getUInt("btn",0x31568A)&0xFFFFFF;
  title=prefs.getString("title","My Remote");
  for(int i=0;i<12;++i)labels[i]=prefs.getString(("label"+String(i)).c_str(),"");
}
static String escape(const String &s){
  String out;
  for(size_t i=0;i<s.length();++i){
    char c=s[i];
    if(c=='&')out+="&amp;";
    else if(c=='<')out+="&lt;";
    else if(c=='>')out+="&gt;";
    else if(c=='"')out+="&quot;";
    else if(c=='\'')out+="&#39;";
    else out+=c;
  }
  return out;
}
static String colorString(uint32_t rgb){
  char buf[8];snprintf(buf,sizeof(buf),"#%06lX",(unsigned long)rgb);
  return String(buf);
}
static bool parseColor(String value,uint32_t &out){
  if(value.length()!=7 || value[0]!='#')return false;
  for(int i=1;i<7;++i)if(!isxdigit((unsigned char)value[i]))return false;
  out=strtoul(value.c_str()+1,nullptr,16);
  return true;
}
static void installRoutes(WebServer &server,bool (*authorized)()){
  server.on("/custom",HTTP_GET,[&server,authorized](){
    if(!authorized())return;
    String html=R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#111827;color:#eee;font:16px system-ui;max-width:600px;margin:20px auto;padding:12px}
input,select,button{padding:10px;margin:6px 0 15px;width:100%;box-sizing:border-box;background:#374151;color:white;border:1px solid #64748b;border-radius:6px}
label{display:block}button{background:#2563eb}section{padding:15px;background:#1f2937;border-radius:10px}</style></head><body>
<h2>Custom Remote</h2><p>The Custom tab appears after you enable and save a layout. Restart the remote to show or hide the tab.</p>
<section><form method="post" action="/custom"><label><input type="checkbox" name="enabled" value="1" style="width:auto" )HTML";
    if(enabled)html+="checked";
    html+=R"HTML(> Show Custom tab</label><label>Tab title</label><input name="title" maxlength="24" value=")HTML";
    html+=escape(title);
    html+=R"HTML("><label>Layout</label><select name="cols"><option value="2")HTML";
    if(columns==2)html+=" selected";
    html+=R"HTML(>2 columns</option><option value="3")HTML";
    if(columns==3)html+=" selected";
    html+=R"HTML(>3 columns</option></select><label>Background color</label><input type="color" name="bg" value=")HTML";
    html+=colorString(background);
    html+=R"HTML("><label>Button color</label><input type="color" name="btn" value=")HTML";
    html+=colorString(buttonColor);
    html+=R"HTML("><p>Enter up to 12 button labels. Empty labels are omitted.</p>)HTML";
    for(int i=0;i<12;++i){
      html+="<label>Button "+String(i+1)+"</label><input name='b"+String(i)+"' maxlength='18' value=\""+escape(labels[i])+"\">";
    }
    html+=R"HTML(<button type="submit">Save custom layout</button></form></section>
<p><a href="/" style="color:#93c5fd">Back to settings</a></p>
<p>Buttons currently show labels and log presses to Serial. Command routing will be added separately.</p></body></html>)HTML";
    server.send(200,"text/html; charset=utf-8",html);
  });
  server.on("/custom",HTTP_POST,[&server,authorized](){
    if(!authorized())return;
    uint32_t bg=0,btn=0;
    if(!parseColor(server.arg("bg"),bg)||!parseColor(server.arg("btn"),btn)){
      server.send(400,"text/plain","Invalid colors");return;
    }
    title=server.arg("title");title.trim();
    if(title.length()>24)title=title.substring(0,24);
    if(title.isEmpty())title="My Remote";
    columns=server.arg("cols")=="3"?3:2;
    background=bg;buttonColor=btn;
    enabled=server.hasArg("enabled");
    count=0;
    for(int i=0;i<12;++i){
      String v=server.arg("b"+String(i));v.trim();
      if(v.length()>18)v=v.substring(0,18);
      if(v.length())labels[count++]=v;
    }
    for(int i=count;i<12;++i)labels[i]="";
    prefs.putBool("enabled",enabled);
    prefs.putUChar("cols",columns);
    prefs.putUChar("count",count);
    prefs.putUInt("bg",background);
    prefs.putUInt("btn",buttonColor);
    prefs.putString("title",title);
    for(int i=0;i<12;++i)prefs.putString(("label"+String(i)).c_str(),labels[i]);
    changed=true;
    server.send(200,"text/html; charset=utf-8","<html><body><h2>Saved</h2><p>Restart the remote to update tab visibility. Existing Custom tab content updates immediately.</p><a href='/custom'>Back to editor</a></body></html>");
  });
}
static void render(lv_obj_t *parent){
  lv_obj_set_style_bg_color(parent,lv_color_hex(background),0);
  lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
  lv_obj_set_style_pad_all(parent,4,0);
  lv_obj_t *heading=lv_label_create(parent);
  lv_label_set_text(heading,title.c_str());
  lv_obj_set_style_text_color(heading,lv_color_white(),0);
  lv_obj_align(heading,LV_ALIGN_TOP_MID,0,3);
  const int buttonW=columns==2?94:60;
  const int buttonH=43;
  const int gap=6;
  for(int i=0;i<count;++i){
    int col=i%columns,row=i/columns;
    lv_obj_t *button=lv_button_create(parent);
    lv_obj_set_size(button,buttonW,buttonH);
    lv_obj_set_pos(button,4+col*(buttonW+gap),32+row*(buttonH+gap));
    lv_obj_set_style_bg_color(button,lv_color_hex(buttonColor),0);
    lv_obj_t *label=lv_label_create(button);
    lv_label_set_text(label,labels[i].c_str());
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(label,buttonW-8);
    lv_obj_center(label);
    lv_obj_add_event_cb(button,[](lv_event_t *e){
      int index=(int)(intptr_t)lv_event_get_user_data(e);
      Serial.printf("[CUSTOM] button %d: %s\n",index+1,labels[index].c_str());
    },LV_EVENT_CLICKED,(void *)(intptr_t)i);
  }
}
}
