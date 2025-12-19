/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * modifications (C) Liam Wilson 2025 under the same license as below
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "jsstddef.h"
#include "jsapi.h"
#include "jscntxt.h"

/* FIXME move this to separate file and impl a win32 polyfill */
#include <dlfcn.h>

#define EXITCODE_RUNTIME_ERROR 3
#define EXITCODE_FILE_NOT_FOUND 4

size_t gStackChunkSize = 8192;
static size_t gMaxStackSize = 0;
static jsuword gStackBase;
int gExitCode = 0;
JSBool gQuitting = JS_FALSE;
FILE *gErrFile = NULL;
FILE *gOutFile = NULL;

static JSBool reportWarnings = JS_TRUE;

typedef enum JSShellErrNum {
#define MSG_DEF(name, number, count, exception, format) \
    name = number,
#include "jsshell.msg"
#undef MSG_DEF
    JSShellErr_Limit
#undef MSGDEF
} JSShellErrNum;

static const JSErrorFormatString *
my_GetErrorMessage(void *userRef, const char *locale, const uintN errorNumber);

static JSBool
GetLine(JSContext *cx, char *bufp, FILE *file, const char *prompt) {
    char line[256];
    fprintf(gOutFile, prompt);
    fflush(gOutFile);
    if (!fgets(line, sizeof line, file))
        return JS_FALSE;
    strcpy(bufp, line);
    return JS_TRUE;
}

static void
Process(JSContext *cx, JSObject *obj, char *filename)
{
    JSBool ok, hitEOF;
    JSScript *script;
    jsval result;
    JSString *str;
    char buffer[4096];
    char *bufp;
    int lineno;
    int startline;
    FILE *file;
    jsuword stackLimit;

    if (!filename || strcmp(filename, "-") == 0) {
        file = stdin;
    } else {
        file = fopen(filename, "r");
        if (!file) {
            JS_ReportErrorNumber(cx, my_GetErrorMessage, NULL,
                                 JSSMSG_CANT_OPEN, filename, strerror(errno));
            gExitCode = EXITCODE_FILE_NOT_FOUND;
            return;
        }
    }

    if (gMaxStackSize == 0) {
        /*
         * Disable checking for stack overflow if limit is zero.
         */
        stackLimit = 0;
    } else {
#if JS_STACK_GROWTH_DIRECTION > 0
        stackLimit = gStackBase + gMaxStackSize;
#else
        stackLimit = gStackBase - gMaxStackSize;
#endif
    }
    JS_SetThreadStackLimit(cx, stackLimit);

    /*
     * It's not interactive - just execute it.
     *
     * Support the UNIX #! shell hack; gobble the first line if it starts
     * with '#'.  TODO - this isn't quite compatible with sharp variables,
     * as a legal js program (using sharp variables) might start with '#'.
     * But that would require multi-character lookahead.
     */
    int ch = fgetc(file);
    if (ch == '#') {
        while((ch = fgetc(file)) != EOF) {
            if (ch == '\n' || ch == '\r')
                break;
        }
    }
    ungetc(ch, file);
    script = JS_CompileFileHandle(cx, obj, filename, file);
    if (script) {
        (void)JS_ExecuteScript(cx, obj, script, &result);
        JS_DestroyScript(cx, script);
    }
    return;
}

static int
usage(void)
{
    fprintf(gErrFile, "%s\n", JS_GetImplementationVersion());
    fprintf(gErrFile, "usage: js [-PswW] [-b branchlimit] [-c stackchunksize] [-v version] [-f scriptfile] [-S maxstacksize] [scriptfile] [scriptarg...]\n");
    return 2;
}

extern JSClass global_class;

static int
ProcessArgs(JSContext *cx, JSObject *obj, char **argv, int argc)
{
    int i, j, length;
    JSObject *argsObj;
    char *filename = NULL;
    JSBool isInteractive = JS_TRUE;

    /*
     * Scan past all optional arguments so we can create the arguments object
     * before processing any -f options, which must interleave properly with
     * -v and -w options.  This requires two passes, and without getopt, we'll
     * have to keep the option logic here and in the second for loop in sync.
     */
    for (i = 0; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            ++i;
            break;
        }
        switch (argv[i][1]) {
          case 'b':
          case 'c':
          case 'f':
          case 'v':
          case 'S':
            ++i;
            break;
        }
    }

    /*
     * Create arguments early and define it to root it, so it's safe from any
     * GC calls nested below, and so it is available to -f <file> arguments.
     */
    argsObj = JS_NewArrayObject(cx, 0, NULL);
    if (!argsObj)
        return 1;
    if (!JS_DefineProperty(cx, obj, "arguments", OBJECT_TO_JSVAL(argsObj),
                           NULL, NULL, 0)) {
        return 1;
    }

    length = argc - i;
    for (j = 0; j < length; j++) {
        JSString *str = JS_NewStringCopyZ(cx, argv[i++]);
        if (!str)
            return 1;
        if (!JS_DefineElement(cx, argsObj, j, STRING_TO_JSVAL(str),
                              NULL, NULL, JSPROP_ENUMERATE)) {
            return 1;
        }
    }

    for (i = 0; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            filename = argv[i++];
            isInteractive = JS_FALSE;
            break;
        }

        switch (argv[i][1]) {
        case 'v':
            if (++i == argc) {
                return usage();
            }
            JS_SetVersion(cx, (JSVersion) atoi(argv[i]));
            break;

        case 'w':
            reportWarnings = JS_TRUE;
            break;

        case 'W':
            reportWarnings = JS_FALSE;
            break;

        case 's':
            JS_ToggleOptions(cx, JSOPTION_STRICT);
            break;

        case 'P':
            if (JS_GET_CLASS(cx, JS_GetPrototype(cx, obj)) != &global_class) {
                JSObject *gobj;

                if (!JS_SealObject(cx, obj, JS_TRUE))
                    return JS_FALSE;
                gobj = JS_NewObject(cx, &global_class, NULL, NULL);
                if (!gobj)
                    return JS_FALSE;
                if (!JS_SetPrototype(cx, gobj, obj))
                    return JS_FALSE;
                JS_SetParent(cx, gobj, NULL);
                JS_SetGlobalObject(cx, gobj);
                obj = gobj;
            }
            break;

        case 'c':
            /* set stack chunk size */
            gStackChunkSize = atoi(argv[++i]);
            break;

        case 'f':
            if (++i == argc) {
                return usage();
            }
            Process(cx, obj, argv[i]);
            /*
             * XXX: js -f foo.js should interpret foo.js and then
             * drop into interactive mode, but that breaks test
             * harness. Just execute foo.js for now.
             */
            isInteractive = JS_FALSE;
            break;

        case 'S':
            if (++i == argc) {
                return usage();
            }
            /* Set maximum stack size. */
            gMaxStackSize = atoi(argv[i]);
            break;

        default:
            return usage();
        }
    }

    if (filename || isInteractive)
        Process(cx, obj, filename);
    return gExitCode;
}


static void
my_LoadErrorReporter(JSContext *cx, const char *message, JSErrorReport *report);

static void
my_ErrorReporter(JSContext *cx, const char *message, JSErrorReport *report);

static JSBool
Load(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    uintN i;
    JSString *str;
    const char *filename;
    JSScript *script;
    JSBool ok;
    jsval result;
    JSErrorReporter older;
    uint32 oldopts;

    for (i = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        argv[i] = STRING_TO_JSVAL(str);
        filename = JS_GetStringBytes(str);
        errno = 0;
        older = JS_SetErrorReporter(cx, my_LoadErrorReporter);
        oldopts = JS_GetOptions(cx);
        JS_SetOptions(cx, oldopts | JSOPTION_COMPILE_N_GO);
        script = JS_CompileFile(cx, obj, filename);
        if (!script) {
            ok = JS_FALSE;
        } else {
            ok = JS_ExecuteScript(cx, obj, script, &result);
            JS_DestroyScript(cx, script);
        }
        JS_SetOptions(cx, oldopts);
        JS_SetErrorReporter(cx, older);
        if (!ok)
            return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
Print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    uintN i, n;
    JSString *str;

    for (i = n = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        fprintf(gOutFile, "%s%s", i ? " " : "", JS_GetStringBytes(str));
    }
    n++;
    if (n)
        fputc('\n', gOutFile);
    return JS_TRUE;
}

static JSBool
Quit(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JS_ConvertArguments(cx, argc, argv,"/ i", &gExitCode);

    gQuitting = JS_TRUE;
    return JS_FALSE;
}

static JSBool
GC(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSRuntime *rt;
    uint32 preBytes;

    rt = cx->runtime;
    preBytes = rt->gcBytes;
    JS_GC(cx);
    fprintf(gOutFile, "before %lu, after %lu, break %08lx\n",
            (unsigned long)preBytes, (unsigned long)rt->gcBytes,
            (unsigned long)sbrk(0)
            );
    return JS_TRUE;
}

JSErrorFormatString jsShell_ErrorFormatString[JSErr_Limit] = {
#if JS_HAS_DFLT_MSG_STRINGS
#define MSG_DEF(name, number, count, exception, format) \
    { format, count } ,
#else
#define MSG_DEF(name, number, count, exception, format) \
    { NULL, count } ,
#endif
#include "jsshell.msg"
#undef MSG_DEF
};

static const JSErrorFormatString *
my_GetErrorMessage(void *userRef, const char *locale, const uintN errorNumber)
{
    if ((errorNumber > 0) && (errorNumber < JSShellErr_Limit))
        return &jsShell_ErrorFormatString[errorNumber];
    return NULL;
}

static void
my_LoadErrorReporter(JSContext *cx, const char *message, JSErrorReport *report)
{
    if (!report) {
        fprintf(gErrFile, "%s\n", message);
        return;
    }

    /* Ignore any exceptions */
    if (JSREPORT_IS_EXCEPTION(report->flags))
        return;

    /* Otherwise, fall back to the ordinary error reporter. */
    my_ErrorReporter(cx, message, report);
}

static void
my_ErrorReporter(JSContext *cx, const char *message, JSErrorReport *report)
{
    int i, j, k, n;
    char *prefix, *tmp;
    const char *ctmp;

    if (!report) {
        fprintf(gErrFile, "%s\n", message);
        return;
    }

    /* Conditionally ignore reported warnings. */
    if (JSREPORT_IS_WARNING(report->flags) && !reportWarnings)
        return;

    prefix = NULL;
    if (report->filename)
        prefix = JS_smprintf("%s:", report->filename);
    if (report->lineno) {
        tmp = prefix;
        prefix = JS_smprintf("%s%u: ", tmp ? tmp : "", report->lineno);
        JS_free(cx, tmp);
    }
    if (JSREPORT_IS_WARNING(report->flags)) {
        tmp = prefix;
        prefix = JS_smprintf("%s%swarning: ",
                             tmp ? tmp : "",
                             JSREPORT_IS_STRICT(report->flags) ? "strict " : "");
        JS_free(cx, tmp);
    }

    /* embedded newlines -- argh! */
    while ((ctmp = strchr(message, '\n')) != 0) {
        ctmp++;
        if (prefix)
            fputs(prefix, gErrFile);
        fwrite(message, 1, ctmp - message, gErrFile);
        message = ctmp;
    }

    /* If there were no filename or lineno, the prefix might be empty */
    if (prefix)
        fputs(prefix, gErrFile);
    fputs(message, gErrFile);

    if (!report->linebuf) {
        fputc('\n', gErrFile);
        goto out;
    }

    /* report->linebuf usually ends with a newline. */
    n = strlen(report->linebuf);
    fprintf(gErrFile, ":\n%s%s%s%s",
            prefix,
            report->linebuf,
            (n > 0 && report->linebuf[n-1] == '\n') ? "" : "\n",
            prefix);
    n = PTRDIFF(report->tokenptr, report->linebuf, char);
    for (i = j = 0; i < n; i++) {
        if (report->linebuf[i] == '\t') {
            for (k = (j + 8) & ~7; j < k; j++) {
                fputc('.', gErrFile);
            }
            continue;
        }
        fputc('.', gErrFile);
        j++;
    }
    fputs("^\n", gErrFile);
 out:
    if (!JSREPORT_IS_WARNING(report->flags))
        gExitCode = EXITCODE_RUNTIME_ERROR;
    JS_free(cx, prefix);
}

static JSBool
global_enumerate(JSContext *cx, JSObject *obj)
{
    return JS_EnumerateStandardClasses(cx, obj);
}

static JSBool
global_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
               JSObject **objp)
{
    if ((flags & JSRESOLVE_ASSIGNING) == 0) {
        JSBool resolved;

        if (!JS_ResolveStandardClass(cx, obj, id, &resolved))
            return JS_FALSE;
        if (resolved) {
            *objp = obj;
            return JS_TRUE;
        }
    }
    return JS_TRUE;
}

JSClass global_class = {
    "global", JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,  JS_PropertyStub,
    JS_PropertyStub,  JS_PropertyStub,
    global_enumerate, (JSResolveOp) global_resolve,
    JS_ConvertStub,   JS_FinalizeStub
};

static JSBool
snarf(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSString *str;
    const char *filename;
    int fd, cc;
    JSBool ok;
    size_t len;
    char *buf;
    struct stat sb;

    str = JS_ValueToString(cx, argv[0]);
    if (!str)
        return JS_FALSE;
    filename = JS_GetStringBytes(str);
    fd = open(filename, O_RDONLY);
    ok = JS_TRUE;
    len = 0;
    buf = NULL;
    if (fd < 0) {
        JS_ReportError(cx, "can't open %s: %s", filename, strerror(errno));
        ok = JS_FALSE;
    } else if (fstat(fd, &sb) < 0) {
        JS_ReportError(cx, "can't stat %s", filename);
        ok = JS_FALSE;
    } else {
        len = sb.st_size;
        buf = JS_malloc(cx, len + 1);
        if (!buf) {
            ok = JS_FALSE;
        } else if ((cc = read(fd, buf, len)) != len) {
            JS_free(cx, buf);
            JS_ReportError(cx, "can't read %s: %s", filename,
                           (cc < 0) ? strerror(errno) : "short read");
            ok = JS_FALSE;
        }
    }
    close(fd);
    if (!ok)
        return ok;
    buf[len] = '\0';
    str = JS_NewString(cx, buf, len);
    if (!str) {
        JS_free(cx, buf);
        return JS_FALSE;
    }
    *rval = STRING_TO_JSVAL(str);
    return JS_TRUE;
}

static JSBool
get_dlsym(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  return JS_NewNumberValue(cx, (double)((int)dlsym), rval);
}

typedef int (* my_ffi_stub)(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8);

static JSBool
ffi_call(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  int v;
  int ptr;
  char* s;
  int args[8];
//  printf("ffi argc: %d\n", argc);
  if(JSVAL_IS_NUMBER(argv[0])) {
    JS_ValueToInt32(cx, argv[0], &ptr);
  } else {
    return JS_FALSE;
  }
  for(int i =1; i<9; i++) {
    rval[0] = argv[i];
    if(JSVAL_IS_NUMBER(rval[0])) {
      JS_ValueToInt32(cx, rval[0], &v);
//      printf("arg %d: 0x%x\n", i, v);
      args[i-1] = v;
    } else  if(JSVAL_IS_STRING(rval[0])) {
      s = JS_GetStringBytes(JSVAL_TO_STRING(argv[i]));
//      printf("arg %d: %s\n", i, s);
      args[i-1] = s;
    } else {
      args[i-1] = 0;
    }
  }

  JS_NewDoubleValue(cx, (double)(((my_ffi_stub)ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7])), rval);
  return JS_TRUE;
}

#include <stdint.h>
uint8_t *heap = 0;

static JSBool
peek8(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  double o;
  JS_ValueToNumber(cx, argv[0], &o);
  JS_NewDoubleValue(cx, (double)heap[(int)o], rval);
  return JS_TRUE;
}

static JSBool
poke8(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  double o;
  double v;
  JS_ValueToNumber(cx, argv[0], &o);
  JS_ValueToNumber(cx, argv[1], &v);
  heap[(int)o] = (int)v & 255;
  return JS_TRUE;
}

static JSBool
peek32(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  double o;
  int *h;
  JS_ValueToNumber(cx, argv[0], &o);
  h = (int)o;
  JS_NewDoubleValue(cx, (double)h[0], rval);
  return JS_TRUE;
}

static JSBool
poke32(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  double o;
  double v;
  int *h;
  JS_ValueToNumber(cx, argv[0], &o);
  h = (int)o;
  JS_ValueToNumber(cx, argv[1], &v);
  h[0] = (int)v;
  return JS_TRUE;
}

static JSFunctionSpec shell_functions[] = {
    {"load",            Load,           1},
    {"print",           Print,          0},
    {"quit",            Quit,           0},
    {"gc",              GC,             0},
    {"read",            snarf,          1},
    {"get_dlsym",       get_dlsym,      0},
    {"ffi_call",        ffi_call,       9},
    {"peek8",           peek8,          0},
    {"poke8",           poke8,          0},
    {"peek32",          peek32,         0},
    {"poke32",          poke32,         0},
    {0}
};

int
main(int argc, char **argv, char **envp)
{
    int stackDummy;
    JSVersion version;
    JSRuntime *rt;
    JSContext *cx;
    JSObject *glob, *envobj;
    int result;

    gStackBase = (jsuword)&stackDummy;

    gErrFile = stderr;
    gOutFile = stdout;

    version = JSVERSION_DEFAULT;

    argc--;
    argv++;

    rt = JS_NewRuntime(64L * 1024L * 1024L);
    if (!rt)
        return 1;

    cx = JS_NewContext(rt, gStackChunkSize);
    if (!cx)
        return 1;
    JS_SetErrorReporter(cx, my_ErrorReporter);

    glob = JS_NewObject(cx, &global_class, NULL, NULL);
    if (!glob)
        return 1;

    JS_SetGlobalObject(cx, glob);

    if (!JS_DefineFunctions(cx, glob, shell_functions))
        return 1;

    /* Set version only after there is a global object. */
    if (version != JSVERSION_DEFAULT)
        JS_SetVersion(cx, version);

    result = ProcessArgs(cx, glob, argv, argc);

    JS_DestroyContext(cx);
    JS_DestroyRuntime(rt);
    JS_ShutDown();
    return result;
}
