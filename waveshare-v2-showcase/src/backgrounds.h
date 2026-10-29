#pragma once
#include <LittleFS.h>
#include <FS.h>
#include <esp_heap_caps.h>

// Four JPEG overrides. Uploaded bytes live in flash; decoded RGB565 stays in PSRAM.
namespace Backgrounds {
static constexpr size_t MAX_JPEG = 400 * 1024;
static bool mounted = false;
static bool changed = false;
static File uploadFile;
static String uploadTemp;
static size_t uploadBytes = 0;
static bool uploadFailed = false;
static int uploadSlot = -1;

static String path(int slot) { return "/bg" + String(slot) + ".jpg"; }
static bool exists(int slot) { return mounted && slot >= 0 && slot < 4 && LittleFS.exists(path(slot)); }
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
<h2>Background Manager</h2><p>Choose a photo or take one with your phone. The browser crops it to 240 x 320 and uploads a JPEG.</p>)HTML";
    h += String("<p>Storage: ") + (mounted ? "Ready" : "Unavailable (filesystem not mounted)") + "</p>";
    if (!mounted) h += "<form method='post' action='/background/format' onsubmit=\"return confirm('Initialize photo storage? This erases existing photo files.')\"><button>Initialize photo storage</button></form>";
    const char *names[] = {"Entertainment", "Bedroom", "Kitchen", "Patio"};
    for (int i = 0; i < 4; ++i) {
      h += "<section><b>" + String(names[i]) + "</b><p>" + (exists(i) ? "Custom photo installed" : "Built-in image") + "</p>";
      if (exists(i)) h += "<img style='width:120px;max-width:100%' src='/background/file?slot=" + String(i) + "'>";
      h += "<button onclick='choose(" + String(i) + ")'>Replace photo</button>";
      if (exists(i)) h += "<button onclick='removeBg(" + String(i) + ")'>Restore built-in</button>";
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
 const blob=await new Promise(r=>canvas.toBlob(r,'image/jpeg',0.83));
 if(!blob)throw Error('JPEG conversion failed');
 status.textContent='Uploading '+blob.size+' bytes...';
 const data=new FormData();data.append('image',blob,'background.jpg');
 const response=await fetch('/background/upload?slot='+slot,{method:'POST',body:data});
 if(!response.ok)throw Error(await response.text());
 status.textContent='Uploaded successfully. Refreshing...';location.reload();
 }catch(e){status.textContent='Error: '+e.message}
 file.value='';
};
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
    if(!exists(slot)){server.send(404,"text/plain","Not found");return;}
    File f=LittleFS.open(path(slot),"r");
    server.streamFile(f,"image/jpeg");
    f.close();
  });
  server.on("/background/delete", HTTP_POST, [&server,authorized]() {
    if(!authorized())return;
    if(!server.hasArg("slot")){server.send(400,"text/plain","Missing slot");return;}
    int slot=server.arg("slot").toInt();
    if(slot<0||slot>3){server.send(400,"text/plain","Invalid slot");return;}
    if(!mounted){server.send(503,"text/plain","Filesystem unavailable");return;}
    LittleFS.remove(path(slot));
    changed=true;
    server.send(200,"text/plain","Restored");
  });
  server.on("/background/upload", HTTP_POST,
    [&server,authorized]() {
      if(!authorized())return;
      if(uploadFailed || uploadSlot<0 || uploadSlot>3 || !mounted) {
        server.send(400,"text/plain","Upload failed; check storage and file size");return;
      }
      if(uploadBytes<4 || !LittleFS.exists(uploadTemp)) {
        server.send(400,"text/plain","Empty upload");return;
      }
      File f=LittleFS.open(uploadTemp,"r");
      uint8_t magic[3]={};
      bool jpeg=f && f.read(magic,3)==3 && magic[0]==0xff && magic[1]==0xd8 && magic[2]==0xff;
      f.close();
      if(!jpeg){LittleFS.remove(uploadTemp);server.send(400,"text/plain","Not a JPEG");return;}
      String dst=path(uploadSlot);
      // Keep previous file until the temporary upload has completed.
      // LittleFS rename can replace an existing destination on ESP32.
      if(!LittleFS.rename(uploadTemp,dst)){
        server.send(500,"text/plain","Could not install image");return;
      }
      changed=true;
      server.send(200,"text/plain","Uploaded");
    },
    [&server,authorized]() {
      if(!authorized())return;
      HTTPUpload &u=server.upload();
      if(u.status==UPLOAD_FILE_START){
        uploadSlot=server.hasArg("slot") ? server.arg("slot").toInt() : -1;
        uploadBytes=0;
        uploadFailed=!mounted || uploadSlot<0 || uploadSlot>3;
        uploadTemp="/incoming.jpg";
        if(!uploadFailed) {
          LittleFS.remove(uploadTemp);
          uploadFile=LittleFS.open(uploadTemp,"w");
          if(!uploadFile)uploadFailed=true;
        }
      }else if(u.status==UPLOAD_FILE_WRITE){
        if(!uploadFailed && uploadBytes+u.currentSize<=MAX_JPEG){
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
static bool read(int slot,uint8_t **data,size_t *length){
  *data=nullptr;*length=0;
  if(!exists(slot))return false;
  File f=LittleFS.open(path(slot),"r");
  if(!f)return false;
  size_t n=f.size();
  if(n<4||n>MAX_JPEG){f.close();return false;}
  uint8_t *buf=(uint8_t*)heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!buf){f.close();return false;}
  bool ok=f.read(buf,n)==n;
  f.close();
  if(!ok){free(buf);return false;}
  *data=buf;*length=n;return true;
}
}
