/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/**
 * \mainpage Mesa GL API Module
 *
 * \section GLAPIIntroduction Introduction
 *
 * The Mesa GL API module is responsible for dispatching all the
 * gl*() functions.  All GL functions are dispatched by jumping through
 * the current dispatch table (basically a struct full of function
 * pointers.)
 *
 * A per-thread current dispatch table and per-thread current context
 * pointer are managed by this module too.
 *
 * This module is intended to be non-Mesa-specific so it can be used
 * with the X/DRI libGL also.
 */


#ifndef _GLAPI_H
#define _GLAPI_H

#include "util/macros.h"
#include "util/u_thread.h"
#include "util/detect_os.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef _WIN32
#  define _GLAPI_EXPORT
#elif defined(__GNUC__)
#  define _GLAPI_EXPORT __attribute__((visibility("default")))
#else
#  define _GLAPI_EXPORT
#endif


typedef void (*_glapi_proc)(void);

typedef void (*_glapi_nop_handler_proc)(const char *name);

struct _glapi_table;

#if DETECT_OS_WINDOWS
extern __THREAD_INITIAL_EXEC struct _glapi_table * _mesa_glapi_tls_Dispatch;
extern __THREAD_INITIAL_EXEC void * _mesa_glapi_tls_Context;
#else
_GLAPI_EXPORT extern __THREAD_INITIAL_EXEC struct _glapi_table * _mesa_glapi_tls_Dispatch;
_GLAPI_EXPORT extern __THREAD_INITIAL_EXEC void * _mesa_glapi_tls_Context;
#endif

#if DETECT_OS_WINDOWS
# define GET_DISPATCH() _mesa_glapi_get_dispatch()
# define GET_CURRENT_CONTEXT(C)  struct gl_context *C = (struct gl_context *) _mesa_glapi_get_context()
#else
# define GET_DISPATCH() _mesa_glapi_tls_Dispatch
# define GET_CURRENT_CONTEXT(C)  struct gl_context *C = (struct gl_context *) _mesa_glapi_tls_Context
#endif


_GLAPI_EXPORT void
_mesa_glapi_set_context(void *context);


_GLAPI_EXPORT void *
_mesa_glapi_get_context(void);


_GLAPI_EXPORT void
_mesa_glapi_set_dispatch(struct _glapi_table *dispatch);


_GLAPI_EXPORT struct _glapi_table *
_mesa_glapi_get_dispatch(void);


_GLAPI_EXPORT unsigned int
_mesa_glapi_get_dispatch_table_size(void);


_GLAPI_EXPORT int
_mesa_glapi_get_proc_offset(const char *funcName);


_GLAPI_EXPORT _glapi_proc
_mesa_glapi_get_proc_address(const char *funcName);


const char *
_glapi_get_proc_name(unsigned int offset);


#if defined(GLX_USE_APPLEGL) || defined(GLX_USE_WINDOWSGL)
_GLAPI_EXPORT struct _glapi_table *
_glapi_create_table_from_handle(void *handle, const char *symbol_prefix);

_GLAPI_EXPORT void
_glapi_table_patch(struct _glapi_table *, const char *name, void *wrapper);
#endif


/** Return pointer to new dispatch table filled with no-op functions */
struct _glapi_table *
_glapi_new_nop_table(void);


#ifdef __cplusplus
}
#endif

#endif /* _GLAPI_H */
