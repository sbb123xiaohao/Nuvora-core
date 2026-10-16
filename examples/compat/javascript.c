/* Small embedding of the unmodified QuickJS engine, not a Node.js runtime. */
#include <stdio.h>
#include <stdlib.h>
#include "quickjs.h"
static JSValue print(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    for(int i=0;i<argc;i++) {
        const char *text=JS_ToCString(ctx,argv[i]);if(!text)return JS_EXCEPTION;
        if(i)putchar(' ');fputs(text,stdout);JS_FreeCString(ctx,text);
    }
    putchar('\n');return JS_UNDEFINED;
}
static int interrupt(JSRuntime *runtime,void *opaque) {
    (void)runtime;unsigned *budget=opaque;return !*budget || !--*budget;
}
static int exception(JSContext *ctx) {
    JSValue error=JS_GetException(ctx);const char *text=JS_ToCString(ctx,error);
    fprintf(stderr,"JavaScript: %s\n",text?text:"exception");
    JS_FreeCString(ctx,text);JS_FreeValue(ctx,error);return 1;
}
int main(int argc,char **argv) {
    if(argc<2) {fputs("js FILE.js [ARGS] (QuickJS core; print and scriptArgs)\n",stderr);return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 2;}
    if(fseek(file,0,SEEK_END)) {fclose(file);return 2;}
    long size=ftell(file);if(size<0 || size>256*1024) {fclose(file);return 2;}rewind(file);
    char *source=malloc(size+1);if(!source) {fclose(file);return 3;}
    if(fread(source,1,size,file)!=(size_t)size) {free(source);fclose(file);return 2;}
    fclose(file);source[size]=0;
    JSRuntime *runtime=JS_NewRuntime();if(!runtime) {free(source);return 3;}
    unsigned budget=10000;JS_SetMemoryLimit(runtime,16*1024*1024);JS_SetMaxStackSize(runtime,128*1024);
    JS_SetCanBlock(runtime,0);JS_SetInterruptHandler(runtime,interrupt,&budget);
    JSContext *ctx=JS_NewContext(runtime);if(!ctx) {free(source);JS_FreeRuntime(runtime);return 3;}
    JSValue global=JS_GetGlobalObject(ctx),args=JS_NewArray(ctx);
    for(int i=1;i<argc;i++)JS_SetPropertyUint32(ctx,args,i-1,JS_NewString(ctx,argv[i]));
    JS_SetPropertyStr(ctx,global,"scriptArgs",args);
    JS_SetPropertyStr(ctx,global,"print",JS_NewCFunction(ctx,print,"print",1));JS_FreeValue(ctx,global);
    JSValue value=JS_Eval(ctx,source,size,argv[1],JS_EVAL_TYPE_GLOBAL);free(source);
    int status=JS_IsException(value)?exception(ctx):0;JS_FreeValue(ctx,value);
    if(!status) {
        JSContext *job_context=NULL;unsigned jobs=0;
        for(;;) {
            if(jobs++==10000) {fputs("JavaScript: pending job limit\n",stderr);status=1;break;}
            int pending=JS_ExecutePendingJob(runtime,&job_context);
            if(pending<0) {status=exception(job_context);break;}
            if(!pending)break;
        }
    }
    JS_FreeContext(ctx);JS_FreeRuntime(runtime);return status;
}
