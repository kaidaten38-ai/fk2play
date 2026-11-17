/* Simule ANativeWindow + JNI pour executer fk2video.c sur PC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include "android/native_window_jni.h"
struct ANativeWindow { int w, h; uint16_t *px; int posts; };
static struct ANativeWindow win;
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* e, jobject s){ return &win; }
/* Le banc force le RGB565 (nativeInit format=1) : tampon de 2 octets par pixel. */
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* w, int32_t a, int32_t b, int32_t f){ w->w=a; w->h=b; free(w->px); w->px=calloc(a*b,2); return 0; }
int32_t ANativeWindow_lock(ANativeWindow* w, ANativeWindow_Buffer* b, void* r){ b->width=w->w; b->height=w->h; b->stride=w->w; b->format=WINDOW_FORMAT_RGB_565; b->bits=w->px; return 0; }
int32_t ANativeWindow_unlockAndPost(ANativeWindow* w){ w->posts++; return 0; }
void ANativeWindow_release(ANativeWindow* w){}
/* JNIEnv minimal : seul GetByteArrayRegion est utilise */
static jbyte *g_arr;
static void JNICALL getRegion(JNIEnv* e, jbyteArray a, jsize off, jsize len, jbyte* buf){ memcpy(buf, g_arr+off, len); }
static jboolean JNICALL noException(JNIEnv* e){ return JNI_FALSE; }
jlong Java_com_fk2play_video_FfmpegDecoder_nativeInit(JNIEnv*, jclass, jint, jint, jint, jint);
jint Java_com_fk2play_video_FfmpegDecoder_nativeSetWindow(JNIEnv*, jclass, jlong, jobject, jboolean);
jint Java_com_fk2play_video_FfmpegDecoder_nativeDecode(JNIEnv*, jclass, jlong, jbyteArray, jint, jint, jboolean);
jint Java_com_fk2play_video_FfmpegDecoder_nativeRenderPending(JNIEnv*, jclass, jlong);
void Java_com_fk2play_video_FfmpegDecoder_nativeRelease(JNIEnv*, jclass, jlong);
/* Decode une unite d'acces puis affiche l'image en attente, comme le fait VideoPipeline. */
static jint decode_show(JNIEnv* env, jlong h, jint off, jint len){
  jint r=Java_com_fk2play_video_FfmpegDecoder_nativeDecode(env,NULL,h,NULL,off,len,JNI_TRUE);
  if(r>0) Java_com_fk2play_video_FfmpegDecoder_nativeRenderPending(env,NULL,h);
  return r;
}
int main(int argc, char** argv){
  struct JNINativeInterface_ fn; memset(&fn,0,sizeof fn); fn.GetByteArrayRegion=getRegion; fn.ExceptionCheck=noException;
  const struct JNINativeInterface_ *envp=&fn; JNIEnv *env=(JNIEnv*)&envp;
  FILE *f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  g_arr=malloc(n); fread(g_arr,1,n,f); fclose(f);
  jlong h=Java_com_fk2play_video_FfmpegDecoder_nativeInit(env,NULL,800,480,4,1);
  if(!h){printf("init KO\n");return 1;}
  Java_com_fk2play_video_FfmpegDecoder_nativeSetWindow(env,NULL,h,(jobject)&win,JNI_FALSE);
  /* Decoupe en unites d'acces comme le dongle (une trame par message, SPS/PPS colles a l'IDR) */
  long start=0; int frames=0, has_slice=0;
  for(long i=0;i+3<n;i++){
    if(g_arr[i]==0&&g_arr[i+1]==0&&g_arr[i+2]==1){
      long sc = (i>0&&g_arr[i-1]==0)? i-1 : i;
      int t=g_arr[i+3]&0x1f;
      int is_slice=(t==1||t==5);
      if(has_slice && (is_slice||t==6||t==7||t==8||t==9)){
        if(decode_show(env,h,(jint)start,(jint)(sc-start))<0) printf("erreur decode\n");
        frames++; start=sc; has_slice=0;
      }
      if(is_slice) has_slice=1;
      i+=2;
    }
  }
  decode_show(env,h,(jint)start,(jint)(n-start)); frames++;
  printf("unites envoyees=%d images rendues=%d fenetre=%dx%d\n",frames,win.posts,win.w,win.h);
  /* Verifie un pixel non noir au centre */
  uint16_t p=win.px[(win.h/2)*win.w+win.w/2]; printf("pixel central RGB565=0x%04x\n",p);
  FILE *o=fopen("frame.raw","wb"); fwrite(win.px,2,win.w*win.h,o); fclose(o);
  Java_com_fk2play_video_FfmpegDecoder_nativeRelease(env,NULL,h);
  return win.posts>0?0:1;
}
