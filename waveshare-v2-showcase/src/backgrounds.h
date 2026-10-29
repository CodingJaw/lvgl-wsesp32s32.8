#pragma once
#include <LittleFS.h>
#include <FS.h>
#include <esp_heap_caps.h>

// Four PNG/JPEG overrides and solid-color backgrounds. Files persist in flash.
namespace Backgrounds {
static constexpr size_t MAX_IMAGE = 700 * 1024;
static bool mounted = false;
static bool changed = false;
static File uploadFile;
static String uploadTemp;
static size_t uploadBytes = 0;
static bool uploadFailed = false;
static int uploadSlot = -1;
static String uploadExt = "jpg";

static String path(int slot, const String &ext = "jpg") { return "/bg" + String(slot) + "." + ext; }
static bool exists(int slot, const String &ext = "jpg") {
  return mounted && slot >= 0 && slot < 4 && LittleFS.exists(path(slot, ext));
}
static String colorPath(int slot) { return path(slot, "color"); }
static String color(int slot) {
  if(!exists(slot,"color")) return "";
  File f=LittleFS.open(colorPath(slot),"r");
  String c=f.readStringUntil('\n');f.close();
  if(c.length()!=6)return "";
  for(size_t i=0;i<6;++i) if(!isxdigit((unsigned char)c[i]))return "";
  return c;
}
static bool setColor(int slot,const String &c) {
  if(!mounted || slot<0 || slot>3 || c.length()!=6)return false;
  for(size_t i=0;i<6;++i)if(!isxdigit((unsigned char)c[i]))return false;
  File f=LittleFS.open(colorPath(slot),"w");
  if(!f)return false;
  bool ok=f.print(c)==6;f.close();
  if(ok)changed=true;
  return ok;
}
static void clearColor(int slot) { if(mounted)LittleFS.remove(colorPath(slot));changed=true; }
static void begin() {
  // Never autoformat: preserve files and leave recovery to a deliberate action.
  mounted = LittleFS.begin(false);
  Serial.printf("[BG] LittleFS: %s\n", mounted ? "mounted" : "not mounted");
}
static void installRoutes(WebServer &server, bool (*authorized)()) {
  server.on("/backgrounds", HTTP_GET, [&server, authorized]() {
    if (!authorized()) return;
    String h = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Background Manager</title><style>body{background:#111827;color:white;font:16px system-ui;max-width:600px;margin:24px auto;padding:15px}
section{background:#1f2937;border-radius:12px;padding:18px;margin:15px 0}button,select,input{padding:12px;margin:8px 0;width:100%;box-sizing:border-box}
button{background:#2563eb;color:white;border:0;border-radius:8px}</style></head><body>
<h2>Background Manager</h2><p>Choose a photo or take one with your phone. The browser crops it to 240 x 320 and uploads a JPEG or PNG.</p>)HTML";
    h += String("<p>Storage: ") + (mounted ? "Ready" : "Unavailable (filesystem not mounted)") + "</p>";
    if (!mounted) h += "<form method='post' action='/background/format' onsubmit=\"return confirm('Initialize photo storage? This erases existing photo files.')\"><button>Initialize photo storage</button></form>";
    const char *names[] = {"Entertainment", "Bedroom", "Kitchen", "Patio"};
    for (int i = 0; i < 4; ++i) {
      String c=color(i);
      h += "<section><b>" + String(names[i]) + "</b><p>" +
        (c.length() ? "Solid color" : exists(i,"png") ? "Custom PNG" : exists(i,"jpg") ? "Custom JPEG" : "Built-in image") + "</p>";
      if(c.length())h += "<div style='width:120px;height:160px;background:#"+c+"'></div>";
      else if(exists(i,"png")||exists(i,"jpg"))
        h += "<img style='width:120px;max-width:100%' src='/background/file?slot=" + String(i) + "'>";
      h += "<label>Background type</label><select id='type"+String(i)+"'><option value='jpg'>JPEG</option><option value='png'>PNG (lossless)</option></select>";
      h += "<button onclick='choose(" + String(i) + ")'>Upload photo</button>";
      h += "<label>Solid background color</label><input type='color' id='color"+String(i)+"' value='#"+(c.length()?c:"202020")+"'>";
      h += "<button onclick='setBgColor(" + String(i) + ")'>Use solid color</button>";
      h += "<button onclick='removeBg(" + String(i) + ")'>Restore built-in</button>";
      h += "</section>";
    }
    h += R"HTML(<input type="file" id="photo" accept="image/*" capture="environment" style="display:none">
<p id="status"></p><p><a style="color:#93c5fd" href="/">Back to settings</a></p>
<script>
let slot=0;const file=document.getElementById('photo'),status=document.getElementById('status');
function choose(n){slot=n;file.click()}
file.onchange=async()=>{
 const src=file.files[0];if(!src)return;
 try {
 status.textContent='Preparing image...';
 const bitmap=await createImageBitmap(src);
 const canvas=document.createElement('canvas');canvas.width=240;canvas.height=320;
 const ctx=canvas.getContext('2d');
 const scale=Math.max(240/bitmap.width,320/bitmap.height);
 const w=bitmap.width*scale,h=bitmap.height*scale;
 ctx.drawImage(bitmap,(240-w)/2,(320-h)/2,w,h);
 bitmap.close();
 const ext=document.getElementById('type'+slot).value;
 const mime=ext==='png'?'image/png':'image/jpeg';
 const blob=await new Promise(r=>canvas.toBlob(r,mime,0.83));
 if(!blob)throw Error('Image conversion failed');
 status.textContent='Uploading '+blob.size+' bytes...';
 const data=new FormData();data.append('image',blob,'background.'+ext);
 const response=await fetch('/background/upload?slot='+slot+'&format='+ext,{method:'POST',body:data});
 if(!response.ok)throw Error(await response.text());
 status.textContent='Uploaded successfully. Refreshing...';location.reload();
 }catch(e){status.textContent='Error: '+e.message}
 file.value='';
};
async function setBgColor(n){
 const hex=document.getElementById('color'+n).value.slice(1);
 const r=await fetch('/background/color?slot='+n+'&hex='+hex,{method:'POST'});
 if(!r.ok){status.textContent=await r.text();return}location.reload();
}
async function removeBg(n){
 if(!confirm('Restore built-in image?'))return;
 const r=await fetch('/background/delete?slot='+n,{method:'POST'});
 if(!r.ok){status.textContent=await r.text();return}location.reload();
}
</script></body></html>)HTML";
    server.send(200,"text/html; charset=utf-8",h);
  });
  server.on("/background/format", HTTP_POST, [&server,authorized]() {
    if(!authorized())return;
    if(mounted){server.send(409,"text/plain","Already mounted; formatting refused");return;}
    mounted=LittleFS.begin(true);
    server.send(mounted?200:500,"text/plain",mounted?"Photo storage initialized. Return to Background Manager.":"Initialization failed");
  });
  server.on("/background/file", HTTP_GET, [&server,authorized]() {
    if(!authorized())return;
    if(!server.hasArg("slot")){server.send(400,"text/plain","Missing slot");return;}
    int slot=server.arg("slot").toInt();
    String ext=exists(slot,"png")?"png":"jpg";
    if(!exists(slot,ext)){server.send(404,"text/plain","Not found");return;}
    File f=LittleFS.open(path(slot,ext),"r");
    server.streamFile(f,ext=="png"?"image/png":"image/jpeg");
    f.close();
  });
  server.on("/background/color", HTTP_POST, [&server,authorized]() {
    if(!authorized())return;
    if(!server.hasArg("slot")){server.send(400,"text/plain","Missing slot");return;}
    int slot=server.arg("slot").toInt();
    if(!setColor(slot,server.arg("hex"))){server.send(400,"text/plain","Invalid color or filesystem");return;}
    server.send(200,"text/plain","Color saved");
  });
  server.on("/background/delete", HTTP_POST, [&server,authorized]() {
    if(!authorized())return;
    if(!server.hasArg("slot")){server.send(400,"text/plain","Missing slot");return;}
    int slot=server.arg("slot").toInt();
    if(slot<0||slot>3){server.send(400,"text/plain","Invalid slot");return;}
    if(!mounted){server.send(503,"text/plain","Filesystem unavailable");return;}
    LittleFS.remove(path(slot,"jpg"));
    LittleFS.remove(path(slot,"png"));
    clearColor(slot);
    changed=true;
    server.send(200,"text/plain","Restored");
  });
  server.on("/background/upload", HTTP_POST,
    [&server,authorized]() {
      if(!authorized())return;
      if(uploadFailed || uploadSlot<0 || uploadSlot>3 || !mounted) {
        server.send(400,"text/plain","Upload failed; check storage and file size");return;
      }
      if(uploadBytes<8 || !LittleFS.exists(uploadTemp)) {
        server.send(400,"text/plain","Empty upload");return;
      }
      File f=LittleFS.open(uploadTemp,"r");
      uint8_t magic[8]={};
      bool header=f && f.read(magic,8)==8;
      f.close();
      bool valid=header && (uploadExt=="jpg"
        ? magic[0]==0xff && magic[1]==0xd8 && magic[2]==0xff
        : magic[0]==137 && magic[1]==80 && magic[2]==78 && magic[3]==71 &&
          magic[4]==13 && magic[5]==10 && magic[6]==26 && magic[7]==10);
      if(!valid){LittleFS.remove(uploadTemp);server.send(400,"text/plain","Image format mismatch");return;}
      String dst=path(uploadSlot,uploadExt);
      // Back up the previous same-format image so failed replacement is recoverable.
      String backup="/previous.bak";
      LittleFS.remove(backup);
      bool hadPrevious=LittleFS.exists(dst);
      if(hadPrevious && !LittleFS.rename(dst,backup)){
        server.send(500,"text/plain","Could not back up previous image");return;
      }
      if(!LittleFS.rename(uploadTemp,dst)){
        if(hadPrevious)LittleFS.rename(backup,dst);
        server.send(500,"text/plain","Could not install image");return;
      }
      LittleFS.remove(backup);
      LittleFS.remove(path(uploadSlot,uploadExt=="jpg"?"png":"jpg"));
      clearColor(uploadSlot);
      changed=true;
      server.send(200,"text/plain","Uploaded");
    },
    [&server,authorized]() {
      if(!authorized())return;
      HTTPUpload &u=server.upload();
      if(u.status==UPLOAD_FILE_START){
        uploadSlot=server.hasArg("slot") ? server.arg("slot").toInt() : -1;
        uploadExt=server.arg("format");
        if(uploadExt!="jpg" && uploadExt!="png")uploadSlot=-1;
        uploadBytes=0;
        uploadFailed=!mounted || uploadSlot<0 || uploadSlot>3;
        uploadTemp="/incoming.tmp";
        if(!uploadFailed) {
          LittleFS.remove(uploadTemp);
          uploadFile=LittleFS.open(uploadTemp,"w");
          if(!uploadFile)uploadFailed=true;
        }
      }else if(u.status==UPLOAD_FILE_WRITE){
        if(!uploadFailed && uploadBytes+u.currentSize<=MAX_IMAGE){
          if(uploadFile.write(u.buf,u.currentSize)!=u.currentSize)uploadFailed=true;
          else uploadBytes+=u.currentSize;
        }else uploadFailed=true;
      }else if(u.status==UPLOAD_FILE_END || u.status==UPLOAD_FILE_ABORTED){
        if(uploadFile)uploadFile.close();
        if(u.status==UPLOAD_FILE_ABORTED || uploadFailed) {
          LittleFS.remove(uploadTemp);
          uploadFailed=true;
        }
      }
    });
}
static bool read(int slot,const String &ext,uint8_t **data,size_t *length){
  *data=nullptr;*length=0;
  if(!exists(slot,ext))return false;
  File f=LittleFS.open(path(slot,ext),"r");
  if(!f)return false;
  size_t n=f.size();
  if(n<4||n>MAX_IMAGE){f.close();return false;}
  uint8_t *buf=(uint8_t*)heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!buf){f.close();return false;}
  bool ok=f.read(buf,n)==n;
  f.close();
  if(!ok){free(buf);return false;}
  *data=buf;*length=n;return true;
}
}
