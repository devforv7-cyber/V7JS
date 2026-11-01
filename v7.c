// Copyright (c) 2013-2022 Cesanta Software Limited
// All rights reserved
// Copyright (c) 2026 MoonLight
//this is a fork of ELK
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License version 3 as
// published by the Free Software Foundation. For the terms of this
// license, see http://www.fsf.org/licensing/licenses/agpl-3.0.html
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, please contact us at https://cesanta.com/contact.html

#define JS_VERSION "4.12.2"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct js;                 // JS engine (opaque)
typedef uint64_t jsval_t;  // JS value
typedef struct js V7;
/* Portable embedding API: V7_Init uses caller-owned memory; V7_Create owns its memory. */
V7 *V7_Init(void *memory, size_t memory_size);
V7 *V7_Create(size_t memory_size);
void V7_Destroy(V7 *vm);
jsval_t V7_Eval(V7 *vm, const char *source, size_t length);
jsval_t V7_EvalCString(V7 *vm, const char *source);
const char *V7_ToString(V7 *vm, jsval_t value);
size_t V7_CopyString(V7 *vm, jsval_t value, char *output, size_t output_size);
int V7_GetType(jsval_t value);
double V7_GetNumber(jsval_t value);
int V7_IsTruthy(V7 *vm, jsval_t value);
void V7_SetGlobal(V7 *vm, const char *name, jsval_t value);

struct js *js_create(void *buf, size_t len);         // Create JS instance
jsval_t js_eval(struct js *, const char *, size_t);  // Execute JS code
jsval_t js_glob(struct js *);                        // Return global object
const char *js_str(struct js *, jsval_t val);        // Stringify JS value
bool js_chkargs(jsval_t *, int, const char *);       // Check args validity
bool js_truthy(struct js *, jsval_t);                // Check if value is true
void js_setmaxcss(struct js *, size_t);              // Set max C stack size
void js_setgct(struct js *, size_t);                 // Set GC trigger threshold
void js_stats(struct js *, size_t *total, size_t *min, size_t *cstacksize);
void js_dump(struct js *);  // Print debug info. Requires -DJS_DUMP

// Create JS values from C values
jsval_t js_mkundef(void);  // Create undefined
jsval_t js_mknull(void);   // Create null, null, true, false
jsval_t js_mktrue(void);   // Create true
jsval_t js_mkfalse(void);  // Create false
jsval_t js_mkstr(struct js *, const void *, size_t);           // Create string
jsval_t js_mknum(double);                                      // Create number
jsval_t js_mkerr(struct js *js, const char *fmt, ...);         // Create error
jsval_t js_mkfun(jsval_t (*fn)(struct js *, jsval_t *, int));  // Create func
jsval_t js_mkobj(struct js *);                                 // Create object
void js_set(struct js *, jsval_t, const char *, jsval_t);      // Set obj attr

// Extract C values from JS values
enum { JS_UNDEF, JS_NULL, JS_TRUE, JS_FALSE, JS_STR, JS_NUM, JS_ERR, JS_PRIV };
int js_type(jsval_t val);       // Return JS value type
double js_getnum(jsval_t val);  // Get number
int js_getbool(jsval_t val);    // Get boolean, 0 or 1
char *js_getstr(struct js *js, jsval_t val, size_t *len);  // Get string

#ifdef __cplusplus
}
#endif
// Copyright (c) 2013-2022 Cesanta Software Limited
// All rights reserved
// Copyright (c) 2026 MoonLight
//this is a fork of ELK
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License version 3 as
// published by the Free Software Foundation. For the terms of this
// license, see http://www.fsf.org/licensing/licenses/agpl-3.0.html
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, please contact us at https://cesanta.com/contact.html

#if defined(__GNUC__) && !defined(JS_OPT) && !defined(ARDUINO_AVR_UNO) && \
    !defined(ARDUINO_AVR_NANO) && !defined(ARDUINO_AVR_PRO) &&            \
    !defined(__APPLE__)
#pragma GCC optimize("O3,inline")
#endif

#include <assert.h>
#include <math.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#ifndef JS_EXPR_MAX
#define JS_EXPR_MAX 20
#endif

#ifndef JS_GC_THRESHOLD
#define JS_GC_THRESHOLD 0.75
#endif

typedef uint32_t jsoff_t;

#define JS_TIMER_MAX 64
#define JS_TIMER_ARGS 8
#define JS_JOB_MAX 256
struct js_timer {
  uint64_t due_ms;
  uint64_t interval_ms;
  uint32_t id;
  uint8_t active, repeat, kind;
  int argc;
  jsval_t callback;
  jsval_t args[JS_TIMER_ARGS];
  jsval_t payload;
  jsval_t promise;
};

struct js {
  jsoff_t css;        // Max observed C stack size
  jsoff_t lwm;        // JS RAM low watermark: min free RAM observed
  const char *code;   // Currently parsed code snippet
  char errmsg[128];    // Error message placeholder
  uint8_t tok;        // Last parsed token value
  uint8_t consumed;   // Indicator that last parsed token was consumed
  uint8_t flags;      // Execution flags, see F_* constants below
#define F_NOEXEC 1U   // Parse code, but not execute
#define F_LOOP 2U     // We're inside the loop
#define F_CALL 4U     // We're inside a function call
#define F_BREAK 8U    // Exit the loop
#define F_RETURN 16U  // Return has been executed
#define F_SWITCH 32U  // We are parsing/executing a switch body
#define F_SWITCH_BREAK 64U  // break exits the nearest switch
#define F_DO_BODY 128U  // permits while(...) after a do-body
  jsoff_t clen;       // Code snippet length
  jsoff_t pos;        // Current parsing position
  jsoff_t toff;       // Offset of the last parsed token
  jsoff_t tlen;       // Length of the last parsed token
  jsoff_t nogc;       // Entity offset to exclude from GC
  jsval_t tval;       // Holds last parsed numeric or string literal value
  jsval_t scope;      // Current scope
  uint8_t *mem;       // Available JS memory
  jsoff_t size;       // Memory size
  jsoff_t brk;        // Current mem usage boundary
  jsoff_t gct;        // GC threshold. If brk > gct, trigger GC
  jsoff_t maxcss;     // Maximum allowed C stack size usage
  void *cstk;         // C stack pointer at the beginning of js_eval()
  jsval_t thrown;     // Value carried by a JavaScript throw statement
  jsval_t this_val;   // Receiver for the currently executing function
  uint8_t has_thrown; // A JavaScript throw is pending
  jsoff_t closure_fn[128];
  jsoff_t closure_scope[128];
  jsoff_t function_props[128];
  jsoff_t function_proto[128];
  uint8_t function_async[128];
  uint8_t function_arrow[128];
  uint8_t function_strict[128];
  jsval_t function_this[128];
  jsoff_t call_caller_scope[128], call_function_scope[128];
  unsigned call_scope_depth;
  uint8_t creating_arrow;
  unsigned closure_count;
  jsoff_t promise_proto;
  jsval_t job_cb[JS_JOB_MAX], job_val[JS_JOB_MAX], job_child[JS_JOB_MAX];
  unsigned job_head, job_count, eval_depth, draining_jobs;
  struct js_timer timers[JS_TIMER_MAX];
  uint32_t next_timer_id;
  uint8_t running_loop;
  uint8_t strict_mode;
  uint8_t decat;
  uint32_t line_locked_entities;
  uint64_t nitrous_passes, wastegate_vents, kinetic_sleep_ms;
  struct { jsoff_t object, property, key; uint32_t hash; } lkp_cache[32];
  unsigned lkp_cache_next;
  char console_timer_labels[16][64];
  uint64_t console_timer_started[16];
  uint8_t console_timer_active[16];
  jsoff_t dm_arrays[256];
  unsigned dm_array_count;
};

// A JS memory stores diffenent entities: objects, properties, strings
// All entities are packed to the beginning of a buffer.
// The `brk` marks the end of the used memory:
//
//    | entity1 | entity2| .... |entityN|         unused memory        |
//    |---------|--------|------|-------|------------------------------|
//  js.mem                           js.brk                        js.size
//
//  Each entity is 4-byte aligned, therefore 2 LSB bits store entity type.
//  Object:   8 bytes: offset of the first property, offset of the upper obj
//  Property: 8 bytes + val: 4 byte next prop, 4 byte key offs, N byte value
//  String:   4xN bytes: 4 byte len << 2, 4byte-aligned 0-terminated data
//
// If C functions are imported, they use the upper part of memory as stack for
// passing params. Each argument is pushed to the top of the memory as jsval_t,
// and js.size is decreased by sizeof(jsval_t), i.e. 8 bytes. When function
// returns, js.size is restored back. So js.size is used as a stack pointer.

// clang-format off
enum { 
  TOK_ERR, TOK_EOF, TOK_IDENTIFIER, TOK_NUMBER, TOK_STRING, TOK_SEMICOLON,
  TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
  // Keyword tokens
  TOK_BREAK = 50, TOK_CASE, TOK_CATCH, TOK_CLASS, TOK_CONST, TOK_CONTINUE,
  TOK_DEFAULT, TOK_DELETE, TOK_DO, TOK_ELSE, TOK_FINALLY, TOK_FOR, TOK_FUNC,
  TOK_IF, TOK_IN, TOK_INSTANCEOF, TOK_LET, TOK_NEW, TOK_RETURN, TOK_SWITCH,
  TOK_THIS, TOK_THROW, TOK_TRY, TOK_VAR, TOK_VOID, TOK_WHILE, TOK_WITH,
  TOK_YIELD, TOK_UNDEF, TOK_NULL, TOK_TRUE, TOK_FALSE, TOK_ASYNC, TOK_AWAIT, TOK_OF,
  // JS Operator tokens
  TOK_DOT = 100, TOK_CALL, TOK_POSTINC, TOK_POSTDEC, TOK_NOT, TOK_TILDA,    // 100
  TOK_TYPEOF, TOK_UPLUS, TOK_UMINUS, TOK_EXP, TOK_MUL, TOK_DIV, TOK_REM,    // 106
  TOK_PLUS, TOK_MINUS, TOK_SHL, TOK_SHR, TOK_ZSHR, TOK_LT, TOK_LE, TOK_GT,  // 113
  TOK_GE, TOK_EQ, TOK_NE, TOK_AND, TOK_XOR, TOK_OR, TOK_LAND, TOK_LOR,      // 121 
  TOK_COLON, TOK_Q,  TOK_ASSIGN, TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,
  TOK_MUL_ASSIGN, TOK_DIV_ASSIGN, TOK_REM_ASSIGN, TOK_SHL_ASSIGN,
  TOK_SHR_ASSIGN, TOK_ZSHR_ASSIGN, TOK_AND_ASSIGN, TOK_XOR_ASSIGN,
  TOK_OR_ASSIGN, TOK_COMMA, TOK_ARROW, TOK_ELLIPSIS, TOK_NULLISH, TOK_OPTCHAIN,
  TOK_EQ2, TOK_NE2, TOK_EXP_ASSIGN, TOK_PREINC, TOK_PREDEC, TOK_NULLISH_ASSIGN,
  TOK_REGEX = 200,
};

enum {
  // IMPORTANT: T_OBJ, T_PROP, T_STR must go first.  That is required by the
  // memory layout functions: memory entity types are encoded in the 2 bits,
  // thus type values must be 0,1,2,3
  T_OBJ, T_PROP, T_STR, T_UNDEF, T_NULL, T_NUM, T_BOOL, T_FUNC, T_CODEREF,
  T_CFUNC, T_ERR, T_HOLE
};

static const char *typestr(uint8_t t) {
  const char *names[] = { "object", "prop", "string", "undefined", "null",
                          "number", "boolean", "function", "coderef",
                          "cfunc", "err", "hole" };
  return (t < sizeof(names) / sizeof(names[0])) ? names[t] : "??";
}

// Pack JS values into uin64_t, double nan, per IEEE 754
// 64bit "double": 1 bit sign, 11 bits exponent, 52 bits mantissa
//
// seeeeeee|eeeemmmm|mmmmmmmm|mmmmmmmm|mmmmmmmm|mmmmmmmm|mmmmmmmm|mmmmmmmm
// 11111111|11110000|00000000|00000000|00000000|00000000|00000000|00000000 inf
// 11111111|11111000|00000000|00000000|00000000|00000000|00000000|00000000 qnan
//
// 11111111|1111tttt|vvvvvvvv|vvvvvvvv|vvvvvvvv|vvvvvvvv|vvvvvvvv|vvvvvvvv
//  NaN marker |type|  48-bit placeholder for values: pointers, strings
//
// On 64-bit platforms, pointers are really 48 bit only, so they can fit,
// provided they are sign extended
static jsval_t tov(double d) {
  /* NaN and infinities occupy the NaN-box tag space. Store explicit numeric
   * sentinels so they can never be mistaken for object/function references. */
  if (isnan(d)) return ((jsval_t) 0x7ff0U << 48U) | ((jsval_t) T_NUM << 48U) | 1U;
  if (isinf(d)) return ((jsval_t) 0x7ff0U << 48U) | ((jsval_t) T_NUM << 48U) | (signbit(d) ? 2U : 3U);
  union { double d; jsval_t v; } u = {d}; return u.v;
}
static double tod(jsval_t v) {
  if ((v >> 52U) == 0x7ffU && ((v >> 48U) & 15U) == T_NUM) {
    uint64_t code = v & 0xffffffffffffUL;
    if (code == 1U) return NAN;
    if (code == 2U) return -INFINITY;
    if (code == 3U) return INFINITY;
  }
  union { jsval_t v; double d; } u = {v}; return u.d;
}
static jsval_t mkval(uint8_t type, uint64_t data) { return ((jsval_t) 0x7ff0U << 48U) | ((jsval_t) (type) << 48) | (data & 0xffffffffffffUL); }
static bool is_nan(jsval_t v) { return (v >> 52U) == 0x7ffU; }
static uint8_t vtype(jsval_t v) { return is_nan(v) ? ((v >> 48U) & 15U) : (uint8_t) T_NUM; }
static size_t vdata(jsval_t v) { return (size_t) (v & ~((jsval_t) 0x7fffUL << 48U)); }
static jsval_t mkcoderef(jsval_t off, jsoff_t len) { return mkval(T_CODEREF, (off & 0xffffffU) | ((jsval_t)(len & 0xffffffU) << 24U)); }
static jsoff_t coderefoff(jsval_t v) { return v & 0xffffffU; }
static jsoff_t codereflen(jsval_t v) { return (v >> 24U) & 0xffffffU; }

static uint8_t unhex(uint8_t c) { return (c >= '0' && c <= '9') ? (uint8_t) (c - '0') : (c >= 'a' && c <= 'f') ? (uint8_t) (c - 'W') : (c >= 'A' && c <= 'F') ? (uint8_t) (c - '7') : 0; }
static bool is_space(int c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == '\v'; }
static bool is_digit(int c) { return c >= '0' && c <= '9'; }
static bool is_xdigit(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_ident_begin(int c) { return c == '_' || c == '$' || is_alpha(c); }
static bool is_ident_continue(int c) { return c == '_' || c == '$' || is_alpha(c) || is_digit(c); }
static bool is_err(jsval_t v) { return vtype(v) == T_ERR; }
static bool is_unary(uint8_t tok) { return tok >= TOK_POSTINC && tok <= TOK_UMINUS; }
static bool is_assign(uint8_t tok) { return (tok >= TOK_ASSIGN && tok <= TOK_OR_ASSIGN) || tok == TOK_EXP_ASSIGN || tok == TOK_NULLISH_ASSIGN; }
static void saveoff(struct js *js, jsoff_t off, jsoff_t val) { memcpy(&js->mem[off], &val, sizeof(val)); }
static void saveval(struct js *js, jsoff_t off, jsval_t val) { memcpy(&js->mem[off], &val, sizeof(val)); }
static jsoff_t loadoff(struct js *js, jsoff_t off) { jsoff_t v = 0; assert(js->brk <= js->size); memcpy(&v, &js->mem[off], sizeof(v)); return v; }
static jsoff_t offtolen(jsoff_t off) { return (off >> 2) - 1; }
static jsoff_t vstrlen(struct js *js, jsval_t v) { return offtolen(loadoff(js, (jsoff_t) vdata(v))); }
static jsval_t loadval(struct js *js, jsoff_t off) { jsval_t v = 0; memcpy(&v, &js->mem[off], sizeof(v)); return v; }
static jsval_t upper(struct js *js, jsval_t scope) { return mkval(T_OBJ, loadoff(js, (jsoff_t) (vdata(scope) + sizeof(jsoff_t)))); }
static jsoff_t align32(jsoff_t v) { return ((v + 3) >> 2) << 2; }

#define CHECKV(_v) do { if (is_err(_v)) { res = (_v); goto done; } } while (0)
#define EXPECT(_tok, _e) do { if (v7_next_token(js) != _tok) { _e; return js_mkerr(js, "parse error"); }; js->consumed = 1; } while (0)
// clang-format on

// Forward declarations of the private functions
static size_t tostr(struct js *js, jsval_t value, char *buf, size_t len);
static bool is_promise(struct js *js, jsval_t value);
static int promise_state(struct js *js, jsval_t promise);
static jsval_t promise_value(struct js *js, jsval_t promise);
static jsval_t js_expr(struct js *js);
static jsval_t js_arrow_expression(struct js *, jsoff_t, jsoff_t, jsoff_t);
static int js_parenthesized_arrow_ahead(struct js *, jsoff_t *, jsoff_t *);
static jsval_t dm_regexp_literal(struct js *, const char *, size_t);
static jsval_t js_stmt(struct js *js);
static jsval_t js_switch(struct js *js);
static jsval_t js_try(struct js *js);
static jsval_t js_class(struct js *js);
static bool dm_has_use_strict(const char *s, size_t n);
static jsval_t function_prototype_value(struct js *js, jsval_t fn);
static jsval_t do_op(struct js *, uint8_t op, jsval_t l, jsval_t r);
static double dm_to_number(struct js *, jsval_t);
static int function_index(struct js *, jsoff_t);
static int dm_const_lvalue(struct js *, jsval_t);
static jsval_t dm_array_new(struct js *);
static void dm_array_store(struct js *, jsval_t, unsigned long, jsval_t);
static jsval_t promise_resolve_native(struct js *, jsval_t *, int);
static jsval_t promise_reject_native(struct js *, jsval_t *, int);
static jsval_t promise_then_native(struct js *, jsval_t *, int);
static jsval_t promise_catch_native(struct js *, jsval_t *, int);
static jsval_t promise_finally_native(struct js *, jsval_t *, int);
static jsval_t dm_object_keys(struct js *, jsval_t *, int);
static jsval_t dm_string_match(struct js *, jsval_t *, int);
static jsval_t dm_string_search(struct js *, jsval_t *, int);
static void drain_microtasks(struct js *);
static void run_event_loop(struct js *, int);
static int run_one_event(struct js *, int);

static void setlwm(struct js *js) {
  jsoff_t n = 0, css = 0;
  if (js->brk < js->size) n = js->size - js->brk;
  if (js->lwm > n) js->lwm = n;
  if ((char *) js->cstk > (char *) &n)
    css = (jsoff_t) ((char *) js->cstk - (char *) &n);
  if (css > js->css) js->css = css;
}

// Copy src to dst, make no overflows, 0-terminate. Return bytes copied
static size_t cpy(char *dst, size_t dstlen, const char *src, size_t srclen) {
  size_t i = 0;
  for (i = 0; i < dstlen && i < srclen && src[i] != 0; i++) dst[i] = src[i];
  if (dstlen > 0) dst[i < dstlen ? i : dstlen - 1] = '\0';
  return i;
}

// Stringify JS object
static size_t strobj(struct js *js, jsval_t obj, char *buf, size_t len) {
  size_t n;
  if (is_promise(js, obj)) {
    int state = promise_state(js, obj);
    n = cpy(buf, len, "Promise { ", 10);
    if (state == 0) n += cpy(buf + n, len > n ? len - n : 0, "<pending>", 9);
    else {
      if (state < 0) n += cpy(buf + n, len > n ? len - n : 0, "<rejected> ", 11);
      n += tostr(js, promise_value(js, obj), buf + n, len > n ? len - n : 0);
    }
    n += cpy(buf + n, len > n ? len - n : 0, " }", 2);
    return n;
  }
  n = cpy(buf, len, "{", 1);
  jsoff_t next = loadoff(js, (jsoff_t) vdata(obj)) & ~3U;  // First prop offset
  while (next < js->brk && next != 0) {                    // Iterate over props
    jsoff_t koff = loadoff(js, next + (jsoff_t) sizeof(next));
    jsval_t val = loadval(js, next + (jsoff_t) (sizeof(next) + sizeof(koff)));
    // printf("PROP %u, koff %u\n", next & ~3, koff);
    n += cpy(buf + n, len - n, ",", n == 1 ? 0 : 1);
    n += tostr(js, mkval(T_STR, koff), buf + n, len - n);
    n += cpy(buf + n, len - n, ":", 1);
    n += tostr(js, val, buf + n, len - n);
    next = loadoff(js, next) & ~3U;  // Load next prop offset
  }
  return n + cpy(buf + n, len - n, "}", 1);
}

// Stringify numeric JS value
static size_t strnum(jsval_t value, char *buf, size_t len) {
  double dv = tod(value), iv;
  if (isnan(dv)) return cpy(buf, len, "NaN", 3);
  if (isinf(dv)) return cpy(buf, len, signbit(dv) ? "-Infinity" : "Infinity", signbit(dv) ? 9 : 8);
  const char *fmt = modf(dv, &iv) == 0.0 ? "%.17g" : "%g";
  return (size_t) snprintf(buf, len, fmt, dv);
}

// Return mem offset and length of the JS string
static jsoff_t vstr(struct js *js, jsval_t value, jsoff_t *len) {
  jsoff_t off = (jsoff_t) vdata(value);
  if (len) *len = offtolen(loadoff(js, off));
  return (jsoff_t) (off + sizeof(off));
}

// Stringify string JS value
static size_t strstring(struct js *js, jsval_t value, char *buf, size_t len) {
  jsoff_t slen, off = vstr(js, value, &slen);
  size_t n = 0;
  n += cpy(buf + n, len - n, "\"", 1);
  n += cpy(buf + n, len - n, (char *) &js->mem[off], slen);
  n += cpy(buf + n, len - n, "\"", 1);
  return n;
}

// Stringify JS function
static size_t strfunc(struct js *js, jsval_t value, char *buf, size_t len) {
  jsoff_t sn, off = vstr(js, value, &sn);
  size_t n = cpy(buf, len, "function", 8);
  return n + cpy(buf + n, len - n, (char *) &js->mem[off], sn);
}

jsval_t js_mkerr(struct js *js, const char *xx, ...) {
  va_list ap;
  size_t n = cpy(js->errmsg, sizeof(js->errmsg), "ERROR: ", 7);
  va_start(ap, xx);
  vsnprintf(js->errmsg + n, sizeof(js->errmsg) - n, xx, ap);
  va_end(ap);
  js->errmsg[sizeof(js->errmsg) - 1] = '\0';
  js->pos = js->clen, js->tok = TOK_EOF, js->consumed = 0;  // Jump to the end
  return mkval(T_ERR, 0);
}

// Stringify JS value into the given buffer
static size_t tostr(struct js *js, jsval_t value, char *buf, size_t len) {
  switch (vtype(value)) {  // clang-format off
    case T_UNDEF: return cpy(buf, len, "undefined", 9);
    case T_NULL:  return cpy(buf, len, "null", 4);
    case T_BOOL:  return cpy(buf, len, vdata(value) & 1 ? "true" : "false", vdata(value) & 1 ? 4 : 5);
    case T_OBJ:   return strobj(js, value, buf, len);
    case T_STR:   return strstring(js, value, buf, len);
    case T_NUM:   return strnum(value, buf, len);
    case T_FUNC:  return strfunc(js, value, buf, len);
    case T_CFUNC: return (size_t) snprintf(buf, len, "\"c_func_0x%lx\"", (unsigned long) vdata(value));
    case T_PROP:  return (size_t) snprintf(buf, len, "PROP@%lu", (unsigned long) vdata(value));
    default:      return (size_t) snprintf(buf, len, "VTYPE%d", vtype(value));
  }  // clang-format on
}

// Stringify JS value into a free JS memory block
const char *js_str(struct js *js, jsval_t value) {
  // Leave jsoff_t placeholder between js->brk and a stringify buffer,
  // in case if next step is convert it into a JS variable
  char *buf = (char *) &js->mem[js->brk + sizeof(jsoff_t)];
  size_t len, available = js->size - js->brk - sizeof(jsoff_t);
  if (is_err(value)) return js->errmsg;
  if (js->brk + sizeof(jsoff_t) >= js->size) return "";
  len = tostr(js, value, buf, available);
  js_mkstr(js, NULL, len);
  return buf;
}

bool js_truthy(struct js *js, jsval_t v) {
  uint8_t t = vtype(v);
  return (t == T_BOOL && vdata(v) != 0) || (t == T_NUM && tod(v) != 0.0) ||
         (t == T_OBJ || t == T_FUNC) || (t == T_STR && vstrlen(js, v) > 0);
}

static jsoff_t js_alloc(struct js *js, size_t size) {
  jsoff_t ofs = js->brk;
  size = align32((jsoff_t) size);  // 4-byte align, (n + k - 1) / k * k
  if (js->brk + size > js->size) return ~(jsoff_t) 0;
  js->brk += (jsoff_t) size;
  return ofs;
}

static jsval_t mkentity(struct js *js, jsoff_t b, const void *buf, size_t len) {
  jsoff_t ofs = js_alloc(js, len + sizeof(b));
  if (ofs == (jsoff_t) ~0) return js_mkerr(js, "oom");
  memcpy(&js->mem[ofs], &b, sizeof(b));
  // Using memmove - in case we're stringifying data from the free JS mem
  if (buf != NULL) memmove(&js->mem[ofs + sizeof(b)], buf, len);
  if ((b & 3) == T_STR) js->mem[ofs + sizeof(b) + len - 1] = 0;  // 0-terminate
  // printf("MKE: %u @ %u type %d\n", js->brk - ofs, ofs, b & 3);
  return mkval(b & 3, ofs);
}

jsval_t js_mkstr(struct js *js, const void *ptr, size_t len) {
  jsoff_t n = (jsoff_t) (len + 1);
  // printf("MKSTR %u %u\n", n, js->brk);
  return mkentity(js, (jsoff_t) ((n << 2) | T_STR), ptr, n);
}

static jsval_t mkobj(struct js *js, jsoff_t parent) {
  return mkentity(js, 0 | T_OBJ, &parent, sizeof(parent));
}

enum { V7_PROP_WRITABLE = 1U, V7_PROP_ENUMERABLE = 2U, V7_PROP_CONFIGURABLE = 4U, V7_PROP_ACCESSOR = 8U };
#define V7_PROP_FLAGS_OFF   ((jsoff_t)(sizeof(jsoff_t) * 2U + sizeof(jsval_t)))
#define V7_PROP_GET_OFF     (V7_PROP_FLAGS_OFF + (jsoff_t)sizeof(uint32_t))
#define V7_PROP_SET_OFF     (V7_PROP_GET_OFF + (jsoff_t)sizeof(jsval_t))
#define V7_PROP_OWNER_OFF   (V7_PROP_SET_OFF + (jsoff_t)sizeof(jsval_t))
#define V7_PROP_DATA_SIZE   (V7_PROP_OWNER_OFF + (jsoff_t)sizeof(jsoff_t))
#define V7_PROP_ENTITY_SIZE ((jsoff_t)sizeof(jsoff_t) + V7_PROP_DATA_SIZE)

static uint32_t prop_flags(struct js *js, jsoff_t off) {
  uint32_t flags = V7_PROP_WRITABLE | V7_PROP_ENUMERABLE | V7_PROP_CONFIGURABLE;
  memcpy(&flags, &js->mem[off + V7_PROP_FLAGS_OFF], sizeof(flags));
  return flags;
}
static jsval_t prop_getter(struct js *js, jsoff_t off) { return loadval(js, off + V7_PROP_GET_OFF); }
static jsval_t prop_setter(struct js *js, jsoff_t off) { return loadval(js, off + V7_PROP_SET_OFF); }
static jsoff_t prop_owner(struct js *js, jsoff_t off) { return loadoff(js, off + V7_PROP_OWNER_OFF); }
static void prop_set_flags(struct js *js, jsoff_t off, uint32_t flags) { memcpy(&js->mem[off + V7_PROP_FLAGS_OFF], &flags, sizeof(flags)); }
static void prop_set_accessors(struct js *js, jsoff_t off, jsval_t getter, jsval_t setter) {
  saveval(js, off + V7_PROP_GET_OFF, getter);
  saveval(js, off + V7_PROP_SET_OFF, setter);
}
static bool v7_same_value(struct js *js, jsval_t a, jsval_t b) {
  if (vtype(a) != vtype(b)) return false;
  if (vtype(a) == T_NUM) {
    double x = tod(a), y = tod(b);
    return (isnan(x) && isnan(y)) || (x == y && (x != 0.0 || signbit(x) == signbit(y)));
  }
  if (vtype(a) == T_STR) {
    size_t an = 0, bn = 0;
    const char *as = js_getstr(js, a, &an), *bs = js_getstr(js, b, &bn);
    return as && bs && an == bn && memcmp(as, bs, an) == 0;
  }
  return a == b;
}

static void v7_lkp_cache_clear(struct js *);
static jsoff_t lkp(struct js *, jsval_t, const char *, size_t);
static jsval_t setprop(struct js *js, jsval_t obj, jsval_t k, jsval_t v) {
  v7_lkp_cache_clear(js);
  jsoff_t koff = (jsoff_t) vdata(k);          // Key offset
  jsoff_t b, head = (jsoff_t) vdata(obj);     // Property list head
  if (head != 0) {
    jsval_t ext = js_mkundef();
    jsoff_t xoff = lkp(js, obj, "__v7_extensible", 15);
    if (xoff) ext = loadval(js, xoff + sizeof(jsoff_t)*2);
    size_t kn = offtolen(loadoff(js, koff)); const char *kp=(const char *)&js->mem[koff+sizeof(jsoff_t)];
    if (vtype(ext)==T_BOOL && !vdata(ext) && !lkp(js,obj,kp,kn)) return js_mkerr(js,"object is not extensible");
  }
  uint8_t buf[V7_PROP_DATA_SIZE];
  uint32_t flags = V7_PROP_WRITABLE | V7_PROP_ENUMERABLE | V7_PROP_CONFIGURABLE;
  memset(buf, 0, sizeof(buf));
  memcpy(&b, &js->mem[head], sizeof(b));
  memcpy(buf, &koff, sizeof(koff));
  memcpy(buf + sizeof(koff), &v, sizeof(v));
  memcpy(buf + V7_PROP_FLAGS_OFF - sizeof(jsoff_t), &flags, sizeof(flags));
  memcpy(buf + V7_PROP_GET_OFF - sizeof(jsoff_t), &(jsval_t){0}, sizeof(jsval_t));
  memcpy(buf + V7_PROP_SET_OFF - sizeof(jsoff_t), &(jsval_t){0}, sizeof(jsval_t));
  jsoff_t owner = (jsoff_t)vdata(obj);
  memcpy(buf + V7_PROP_OWNER_OFF - sizeof(jsoff_t), &owner, sizeof(owner));
  jsoff_t brk = js->brk | T_OBJ;
  jsval_t res = mkentity(js, (b & ~3U) | T_PROP, buf, sizeof(buf));
  if (!is_err(res))
    memcpy(&js->mem[head], &brk, sizeof(brk));  // Repoint head to the new prop
  return res;
}

// Return T_OBJ/T_PROP/T_STR entity size based on the first word in memory
static inline jsoff_t esize(jsoff_t w) {
  switch (w & 3U) {  // clang-format off
    case T_OBJ:   return (jsoff_t) (sizeof(jsoff_t) + sizeof(jsoff_t));
    case T_PROP:  return V7_PROP_ENTITY_SIZE;
    case T_STR:   return (jsoff_t) (sizeof(jsoff_t) + align32(w >> 2U));
    default:      return (jsoff_t) ~0U;
  }  // clang-format on
}

static bool is_mem_entity(uint8_t t) {
  return t == T_OBJ || t == T_PROP || t == T_STR || t == T_FUNC;
}

#define GCMASK ~(((jsoff_t) ~0) >> 1)  // Entity deletion marker
static void js_fixup_offsets(struct js *js, jsoff_t start, jsoff_t size) {
  for (jsoff_t n, v, off = 0; off < js->brk; off += n) {  // start from 0!
    v = loadoff(js, off);
    n = esize(v & ~GCMASK);
    if (v & GCMASK) continue;  // To be deleted, don't bother
    if ((v & 3) != T_OBJ && (v & 3) != T_PROP) continue;
    if (v > start) saveoff(js, off, v - size);
    if ((v & 3) == T_OBJ) {
      jsoff_t u = loadoff(js, (jsoff_t) (off + sizeof(jsoff_t)));
      if (u > start) saveoff(js, (jsoff_t) (off + sizeof(jsoff_t)), u - size);
    }
    if ((v & 3) == T_PROP) {
      jsoff_t koff = loadoff(js, (jsoff_t) (off + sizeof(off)));
      if (koff > start) saveoff(js, (jsoff_t) (off + sizeof(off)), koff - size);
      jsval_t val = loadval(js, (jsoff_t) (off + sizeof(off) + sizeof(off)));
      if (is_mem_entity(vtype(val)) && vdata(val) > start) {
        saveval(js, (jsoff_t) (off + sizeof(off) + sizeof(off)),
                mkval(vtype(val), (unsigned long) (vdata(val) - size)));
      }
      {
        jsval_t getter = loadval(js, off + V7_PROP_GET_OFF);
        jsval_t setter = loadval(js, off + V7_PROP_SET_OFF);
        if (is_mem_entity(vtype(getter)) && vdata(getter) > start)
          saveval(js, off + V7_PROP_GET_OFF, mkval(vtype(getter), vdata(getter) - size));
        if (is_mem_entity(vtype(setter)) && vdata(setter) > start)
          saveval(js, off + V7_PROP_SET_OFF, mkval(vtype(setter), vdata(setter) - size));
        jsoff_t owner = loadoff(js, off + V7_PROP_OWNER_OFF);
        if (owner > start) saveoff(js, off + V7_PROP_OWNER_OFF, owner - size);
      }
    }
  }
  // Fixup js->scope
  jsoff_t off = (jsoff_t) vdata(js->scope);
  if (off > start) js->scope = mkval(T_OBJ, off - size);
  if (js->promise_proto > start) js->promise_proto -= size;
  for (unsigned i = 0; i < JS_JOB_MAX; i++) {
    jsval_t vals[3] = {js->job_cb[i], js->job_val[i], js->job_child[i]};
    for (unsigned k = 0; k < 3; k++) if (is_mem_entity(vtype(vals[k])) && vdata(vals[k]) > start) vals[k] = mkval(vtype(vals[k]), vdata(vals[k]) - size);
    js->job_cb[i] = vals[0]; js->job_val[i] = vals[1]; js->job_child[i] = vals[2];
  }
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) {
    struct js_timer *t = &js->timers[i];
    jsval_t *vals[] = {&t->callback, &t->payload, &t->promise};
    for (unsigned k = 0; k < 3; k++) if (is_mem_entity(vtype(*vals[k])) && vdata(*vals[k]) > start) *vals[k] = mkval(vtype(*vals[k]), vdata(*vals[k]) - size);
    for (int k = 0; k < t->argc && k < JS_TIMER_ARGS; k++) if (is_mem_entity(vtype(t->args[k])) && vdata(t->args[k]) > start) t->args[k] = mkval(vtype(t->args[k]), vdata(t->args[k]) - size);
  }
  for (unsigned i = 0; i < js->dm_array_count; i++) {
    if (js->dm_arrays[i] > start) js->dm_arrays[i] -= size;
  }
  for (unsigned i = 0; i < js->closure_count; i++) {
    if (js->closure_fn[i] > start) js->closure_fn[i] -= size;
    if (js->closure_scope[i] > start) js->closure_scope[i] -= size;
    if (js->function_props[i] > start) js->function_props[i] -= size;
    if (js->function_proto[i] > start) js->function_proto[i] -= size;
    if (is_mem_entity(vtype(js->function_this[i])) && vdata(js->function_this[i]) > start)
      js->function_this[i] = mkval(vtype(js->function_this[i]), vdata(js->function_this[i]) - size);
  }
  for (unsigned i = 0; i < js->call_scope_depth; i++) {
    if (js->call_caller_scope[i] > start) js->call_caller_scope[i] -= size;
    if (js->call_function_scope[i] > start) js->call_function_scope[i] -= size;
  }
  /* Values held in C-side interpreter state are roots too.  Their entity
   * offsets must follow compaction just like object properties do. */
  {
    jsval_t *roots[] = {&js->tval, &js->this_val, &js->thrown};
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
      jsval_t value = *roots[i];
      if (i == 2 && !js->has_thrown) continue;
      if (is_mem_entity(vtype(value)) && vdata(value) > start) {
        *roots[i] = mkval(vtype(value), (unsigned long) (vdata(value) - size));
      }
    }
  }
  if (js->nogc >= start) js->nogc -= size;
  // Fixup code that we're executing now, if required
  if (js->code > (char *) js->mem && js->code - (char *) js->mem < js->size &&
      js->code - (char *) js->mem > start) {
    js->code -= size;
    // printf("GC-ing code under us!! %ld\n", js->code - (char *) js->mem);
  }
  // printf("FIXEDOFF %u %u\n", start, size);
}

static void js_delete_marked_entities(struct js *js) {
  for (jsoff_t n, v, off = 0; off < js->brk; off += n) {
    v = loadoff(js, off);
    n = esize(v & ~GCMASK);
    if (v & GCMASK) {  // This entity is marked for deletion, remove it
      // printf("DEL: %4u %d %x\n", off, v & 3, n);
      // assert(off + n <= js->brk);
      js_fixup_offsets(js, off, n);
      memmove(&js->mem[off], &js->mem[off + n], js->brk - off - n);
      js->brk -= n;  // Shrink brk boundary by the size of deleted entity
      n = 0;         // We shifted data, next iteration stay on this offset
    }
  }
}

static void js_mark_all_entities_for_deletion(struct js *js) {
  for (jsoff_t v, off = 0; off < js->brk; off += esize(v)) {
    v = loadoff(js, off);
    saveoff(js, off, v | GCMASK);
  }
}

static jsoff_t js_unmark_entity(struct js *js, jsoff_t off) {
  jsoff_t v = loadoff(js, off);
  if (v & GCMASK) {
    saveoff(js, off, v & ~GCMASK);
    // printf("UNMARK %5u %d\n", off, v & 3);
    if ((v & 3) == T_OBJ) {
      js_unmark_entity(js, v & ~(GCMASK | 3));
      jsoff_t parent = loadoff(js, (jsoff_t) (off + sizeof(jsoff_t)));
      if (parent != 0 && parent != off) js_unmark_entity(js, parent);
    }
    if ((v & 3) == T_PROP) {
      js_unmark_entity(js, v & ~(GCMASK | 3));  // Unmark next prop
      js_unmark_entity(js, loadoff(js, (jsoff_t) (off + sizeof(off))));  // key
      jsval_t val = loadval(js, (jsoff_t) (off + sizeof(off) + sizeof(off)));
      if (is_mem_entity(vtype(val))) js_unmark_entity(js, (jsoff_t) vdata(val));
      jsval_t getter = loadval(js, off + V7_PROP_GET_OFF);
      jsval_t setter = loadval(js, off + V7_PROP_SET_OFF);
      if (is_mem_entity(vtype(getter))) js_unmark_entity(js, (jsoff_t)vdata(getter));
      if (is_mem_entity(vtype(setter))) js_unmark_entity(js, (jsoff_t)vdata(setter));
    }
  }
  return v & ~(GCMASK | 3U);
}

static void js_unmark_used_entities(struct js *js) {
  jsval_t scope = js->scope;
  do {
    js_unmark_entity(js, (jsoff_t) vdata(scope));
    scope = upper(js, scope);
  } while (vdata(scope) != 0);  // When global scope is GC-ed, stop
  if (js->nogc) js_unmark_entity(js, js->nogc);
  for (unsigned i = 0; i < js->job_count; i++) {
    unsigned at = (js->job_head + i) % JS_JOB_MAX;
    if (is_mem_entity(vtype(js->job_cb[at]))) js_unmark_entity(js, (jsoff_t) vdata(js->job_cb[at]));
    if (is_mem_entity(vtype(js->job_val[at]))) js_unmark_entity(js, (jsoff_t) vdata(js->job_val[at]));
    if (is_mem_entity(vtype(js->job_child[at]))) js_unmark_entity(js, (jsoff_t) vdata(js->job_child[at]));
  }
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (js->timers[i].active) {
    struct js_timer *t = &js->timers[i];
    jsval_t vals[] = {t->callback, t->payload, t->promise};
    for (unsigned k = 0; k < 3; k++) if (is_mem_entity(vtype(vals[k]))) js_unmark_entity(js, (jsoff_t) vdata(vals[k]));
    for (int k = 0; k < t->argc && k < JS_TIMER_ARGS; k++) if (is_mem_entity(vtype(t->args[k]))) js_unmark_entity(js, (jsoff_t) vdata(t->args[k]));
  }
  if (js->promise_proto) js_unmark_entity(js, js->promise_proto);
  /* Keep parser/interpreter temporaries alive across a collection.  These
   * can contain freshly-created strings or objects not yet stored in scope. */
  if (is_mem_entity(vtype(js->tval))) js_unmark_entity(js, (jsoff_t) vdata(js->tval));
  if (is_mem_entity(vtype(js->this_val))) js_unmark_entity(js, (jsoff_t) vdata(js->this_val));
  if (js->has_thrown && is_mem_entity(vtype(js->thrown))) js_unmark_entity(js, (jsoff_t) vdata(js->thrown));
  for (unsigned i = 0; i < js->dm_array_count; i++) js_unmark_entity(js, js->dm_arrays[i]);
  for (unsigned i = 0; i < js->closure_count; i++) {
    js_unmark_entity(js, js->closure_fn[i]);
    js_unmark_entity(js, js->closure_scope[i]);
    js_unmark_entity(js, js->function_props[i]);
    js_unmark_entity(js, js->function_proto[i]);
    if (is_mem_entity(vtype(js->function_this[i]))) js_unmark_entity(js, (jsoff_t)vdata(js->function_this[i]));
  }
  for (unsigned i = 0; i < js->call_scope_depth; i++) {
    js_unmark_entity(js, js->call_caller_scope[i]);
    js_unmark_entity(js, js->call_function_scope[i]);
  }
  // printf("UNMARK: nogc %u\n", js->nogc);
  // js_dump(js);
}

void js_gc(struct js *js) {
  // printf("================== GC %u\n", js->nogc);
  setlwm(js);
  if (js->nogc == (jsoff_t) ~0) return;  // ~0 is a special case: GC Is disabled
  js_mark_all_entities_for_deletion(js);
  js_unmark_used_entities(js);
  js_delete_marked_entities(js);
  v7_lkp_cache_clear(js);
  /* Re-arm the collector relative to the live heap, rather than leaving a
   * permanently low watermark that causes a full collection on every later
   * statement. Keep a little headroom for small heaps, capped at js->size. */
  {
    jsoff_t free_bytes = js->size > js->brk ? js->size - js->brk : 0;
    jsoff_t growth = free_bytes / 2U;
    if (growth > 0 && growth < 1024U) growth = free_bytes < 1024U ? free_bytes : 1024U;
    js->gct = js->brk + growth;
    if (js->gct > js->size) js->gct = js->size;
  }
}

// Skip whitespaces and comments
static jsoff_t skiptonext(const char *code, jsoff_t len, jsoff_t n) {
  // printf("SKIP: [%.*s]\n", len - n, &code[n]);
  while (n < len) {
    if (is_space(code[n])) {
      n++;
    } else if (n + 1 < len && code[n] == '/' && code[n + 1] == '/') {
      for (n += 2; n < len && code[n] != '\n';) n++;
    } else if (n + 3 < len && code[n] == '/' && code[n + 1] == '*') {
      for (n += 4; n < len && (code[n - 2] != '*' || code[n - 1] != '/');) n++;
    } else {
      break;
    }
  }
  return n;
}

static bool streq(const char *buf, size_t len, const char *p, size_t n) {
  return n == len && memcmp(buf, p, len) == 0;
}

static uint8_t parsekeyword(const char *buf, size_t len) {
  switch (buf[0]) {  // clang-format off
    case 'a': if (streq("async", 5, buf, len)) return TOK_ASYNC; if (streq("await", 5, buf, len)) return TOK_AWAIT; break;
    case 'b': if (streq("break", 5, buf, len)) return TOK_BREAK; break;
    case 'c': if (streq("class", 5, buf, len)) return TOK_CLASS; if (streq("case", 4, buf, len)) return TOK_CASE; if (streq("catch", 5, buf, len)) return TOK_CATCH; if (streq("const", 5, buf, len)) return TOK_CONST; if (streq("continue", 8, buf, len)) return TOK_CONTINUE; break;
    case 'd': if (streq("do", 2, buf, len)) return TOK_DO; if (streq("default", 7, buf, len)) return TOK_DEFAULT; if (streq("delete", 6, buf, len)) return TOK_DELETE; break;
    case 'e': if (streq("else", 4, buf, len)) return TOK_ELSE; break;
    case 'f': if (streq("for", 3, buf, len)) return TOK_FOR; if (streq("function", 8, buf, len)) return TOK_FUNC; if (streq("finally", 7, buf, len)) return TOK_FINALLY; if (streq("false", 5, buf, len)) return TOK_FALSE; break;
    case 'i': if (streq("if", 2, buf, len)) return TOK_IF; if (streq("in", 2, buf, len)) return TOK_IN; if (streq("instanceof", 10, buf, len)) return TOK_INSTANCEOF; break;
    case 'l': if (streq("let", 3, buf, len)) return TOK_LET; break;
    case 'n': if (streq("new", 3, buf, len)) return TOK_NEW; if (streq("null", 4, buf, len)) return TOK_NULL; break;
    case 'o': if (streq("of", 2, buf, len)) return TOK_OF; break;
    case 'r': if (streq("return", 6, buf, len)) return TOK_RETURN; break;
    case 's': if (streq("switch", 6, buf, len)) return TOK_SWITCH; break;
    case 't': if (streq("try", 3, buf, len)) return TOK_TRY; if (streq("this", 4, buf, len)) return TOK_THIS; if (streq("throw", 5, buf, len)) return TOK_THROW; if (streq("true", 4, buf, len)) return TOK_TRUE; if (streq("typeof", 6, buf, len)) return TOK_TYPEOF; break;
    case 'u': if (streq("undefined", 9, buf, len)) return TOK_UNDEF; break;
    case 'v': if (streq("var", 3, buf, len)) return TOK_VAR; if (streq("void", 4, buf, len)) return TOK_VOID; break;
    case 'w': if (streq("while", 5, buf, len)) return TOK_WHILE; if (streq("with", 4, buf, len)) return TOK_WITH; break;
    case 'y': if (streq("yield", 5, buf, len)) return TOK_YIELD; break;
  }  // clang-format on
  return TOK_IDENTIFIER;
}

static uint8_t parseident(const char *buf, jsoff_t len, jsoff_t *tlen) {
  if (is_ident_begin(buf[0])) {
    while (*tlen < len && is_ident_continue(buf[*tlen])) (*tlen)++;
    return parsekeyword(buf, *tlen);
  }
  return TOK_ERR;
}

static int dm_regex_literal_allowed(uint8_t previous) {
  switch (previous) {
    case TOK_ERR: case TOK_EOF: case TOK_SEMICOLON: case TOK_LPAREN:
    case TOK_LBRACE: case TOK_LBRACKET: case TOK_COMMA: case TOK_COLON:
    case TOK_Q: case TOK_CALL: case TOK_ASSIGN: case TOK_PLUS_ASSIGN: case TOK_MINUS_ASSIGN:
    case TOK_MUL_ASSIGN: case TOK_DIV_ASSIGN: case TOK_REM_ASSIGN:
    case TOK_SHL_ASSIGN: case TOK_SHR_ASSIGN: case TOK_ZSHR_ASSIGN:
    case TOK_AND_ASSIGN: case TOK_XOR_ASSIGN: case TOK_OR_ASSIGN:
    case TOK_EXP_ASSIGN: case TOK_RETURN: case TOK_THROW: case TOK_CASE:
    case TOK_NOT: case TOK_TILDA: case TOK_PLUS: case TOK_MINUS: case TOK_MUL:
    case TOK_DIV: case TOK_REM: case TOK_EXP: case TOK_SHL: case TOK_SHR:
    case TOK_ZSHR: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
    case TOK_EQ: case TOK_NE: case TOK_EQ2: case TOK_NE2: case TOK_AND:
    case TOK_XOR: case TOK_OR: case TOK_LAND: case TOK_LOR: case TOK_NULLISH:
    case TOK_ARROW:
      return 1;
    default:
      return 0;
  }
}

static uint8_t v7_next_token(struct js *js) {
  if (js->consumed == 0) return js->tok;
  uint8_t previous_tok = js->tok;
  js->consumed = 0;
  js->tok = TOK_ERR;
  js->toff = js->pos = skiptonext(js->code, js->clen, js->pos);
  js->tlen = 0;
  const char *buf = js->code + js->toff;
  // clang-format off
  if (js->toff >= js->clen) { js->tok = TOK_EOF; return js->tok; }
#define TOK(T, LEN) { js->tok = T; js->tlen = (LEN); break; }
#define LOOK(OFS, CH) js->toff + OFS < js->clen && buf[OFS] == CH
  switch (buf[0]) {
    case '?': if (LOOK(1, '?') && LOOK(2, '=')) TOK(TOK_NULLISH_ASSIGN, 3); if (LOOK(1, '?')) TOK(TOK_NULLISH, 2); if (LOOK(1, '.')) TOK(TOK_OPTCHAIN, 2); TOK(TOK_Q, 1);
    case ':': TOK(TOK_COLON, 1);
    case '(': TOK(TOK_LPAREN, 1);
    case ')': TOK(TOK_RPAREN, 1);
    case '{': TOK(TOK_LBRACE, 1);
    case '}': TOK(TOK_RBRACE, 1);
    case '[': TOK(TOK_LBRACKET, 1);
    case ']': TOK(TOK_RBRACKET, 1);
    case ';': TOK(TOK_SEMICOLON, 1);
    case ',': TOK(TOK_COMMA, 1);
    case '!': if (LOOK(1, '=') && LOOK(2, '=')) TOK(TOK_NE, 3); if (LOOK(1, '=')) TOK(TOK_NE2, 2); TOK(TOK_NOT, 1);
    case '.':
      if (LOOK(1, '.') && LOOK(2, '.')) TOK(TOK_ELLIPSIS, 3);
      if (js->toff + 1 < js->clen && is_digit((unsigned char) buf[1])) {
        char number[256], *end;
        size_t remaining = (size_t) js->clen - (size_t) js->toff;
        size_t take = remaining < sizeof(number) - 1 ? remaining : sizeof(number) - 1;
        memcpy(number, buf, take); number[take] = '\0';
        double value = strtod(number, &end);
        if (end == number + 1 || (take == sizeof(number) - 1 && (size_t)(end-number) == take && take < remaining)) { js->tok = TOK_ERR; break; }
        js->tval = tov(value);
        TOK(TOK_NUMBER, (jsoff_t)(end - number));
      }
      TOK(TOK_DOT, 1);
    case '~': TOK(TOK_TILDA, 1);
    case '-': if (LOOK(1, '-')) TOK(TOK_POSTDEC, 2); if (LOOK(1, '=')) TOK(TOK_MINUS_ASSIGN, 2); TOK(TOK_MINUS, 1);
    case '+': if (LOOK(1, '+')) TOK(TOK_POSTINC, 2); if (LOOK(1, '=')) TOK(TOK_PLUS_ASSIGN, 2); TOK(TOK_PLUS, 1);
    case '*': if (LOOK(1, '*') && LOOK(2, '=')) TOK(TOK_EXP_ASSIGN, 3); if (LOOK(1, '*')) TOK(TOK_EXP, 2); if (LOOK(1, '=')) TOK(TOK_MUL_ASSIGN, 2); TOK(TOK_MUL, 1);
    case '/':
      if (dm_regex_literal_allowed(previous_tok)) {
        size_t i = 1; int escaped = 0, in_class = 0, closed = 0;
        while (js->toff + i < js->clen) {
          unsigned char c = (unsigned char)buf[i];
          if (c == '\n' || c == '\r') break;
          if (escaped) { escaped = 0; i++; continue; }
          if (c == '\\') { escaped = 1; i++; continue; }
          if (c == '[') in_class = 1;
          else if (c == ']') in_class = 0;
          else if (c == '/' && !in_class) { closed = 1; i++; break; }
          i++;
        }
        if (!closed) { js->tok = TOK_ERR; break; }
        while (js->toff + i < js->clen && is_alpha((unsigned char)buf[i])) i++;
        TOK(TOK_REGEX, i);
      }
      if (LOOK(1, '=')) TOK(TOK_DIV_ASSIGN, 2);
      TOK(TOK_DIV, 1);
    case '%': if (LOOK(1, '=')) TOK(TOK_REM_ASSIGN, 2); TOK(TOK_REM, 1);
    case '&': if (LOOK(1, '&')) TOK(TOK_LAND, 2); if (LOOK(1, '=')) TOK(TOK_AND_ASSIGN, 2); TOK(TOK_AND, 1);
    case '|': if (LOOK(1, '|')) TOK(TOK_LOR, 2); if (LOOK(1, '=')) TOK(TOK_OR_ASSIGN, 2); TOK(TOK_OR, 1);
    case '=': if (LOOK(1, '>')) TOK(TOK_ARROW, 2); if (LOOK(1, '=') && LOOK(2, '=')) TOK(TOK_EQ, 3); if (LOOK(1, '=')) TOK(TOK_EQ2, 2); TOK(TOK_ASSIGN, 1);
    case '<': if (LOOK(1, '<') && LOOK(2, '=')) TOK(TOK_SHL_ASSIGN, 3); if (LOOK(1, '<')) TOK(TOK_SHL, 2); if (LOOK(1, '=')) TOK(TOK_LE, 2); TOK(TOK_LT, 1);
    case '>': if (LOOK(1, '>') && LOOK(2, '>') && LOOK(3, '=')) TOK(TOK_ZSHR_ASSIGN, 4); if (LOOK(1, '>') && LOOK(2, '>')) TOK(TOK_ZSHR, 3); if (LOOK(1, '>') && LOOK(2, '=')) TOK(TOK_SHR_ASSIGN, 3); if (LOOK(1, '>')) TOK(TOK_SHR, 2); if (LOOK(1, '=')) TOK(TOK_GE, 2); TOK(TOK_GT, 1);
    case '^': if (LOOK(1, '=')) TOK(TOK_XOR_ASSIGN, 2); TOK(TOK_XOR, 1);
    case '"': case '\'': case '`':
      js->tlen++;
      while (js->toff + js->tlen < js->clen && buf[js->tlen] != buf[0]) {
        uint8_t increment = 1;
        if (buf[js->tlen] == '\\') {
          if (js->toff + js->tlen + 2 >= js->clen) break;
          increment = 2;
          if (buf[js->tlen + 1] == 'x') {
            if (js->toff + js->tlen + 4 >= js->clen) break;
            increment = 4;
          } else if (buf[js->tlen + 1] == 'u') {
            if (js->toff + js->tlen + 6 >= js->clen) break;
            increment = 6;
          }
        }
        js->tlen += increment;
      }
      if (js->toff + js->tlen < js->clen && buf[0] == buf[js->tlen]) js->tok = TOK_STRING, js->tlen++;
      break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
      if (buf[0] == '0' && js->toff + 1 < js->clen &&
          (buf[1] == 'b' || buf[1] == 'B' || buf[1] == 'o' || buf[1] == 'O')) {
        unsigned base = (buf[1] == 'b' || buf[1] == 'B') ? 2U : 8U;
        jsoff_t i = 2; double value = 0.0; bool invalid = false;
        while (js->toff + i < js->clen) {
          unsigned digit;
          int c = (unsigned char) buf[i];
          if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
          else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
          else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
          else break;
          if (digit >= base) { invalid = true; break; }
          value = value * (double) base + (double) digit;
          i++;
        }
        if (invalid) { js->tok = TOK_ERR; break; }
        if (i == 2 || (js->toff + i < js->clen && is_ident_continue(buf[i]))) {
          js->tok = TOK_ERR; break;
        }
        js->tval = tov(value);
        TOK(TOK_NUMBER, i);
      }
      char number[256], normalized[256], *end;
      size_t remaining = (size_t) js->clen - (size_t) js->toff;
      size_t take = remaining < sizeof(number) - 1 ? remaining : sizeof(number) - 1;
      memcpy(number, buf, take);
      number[take] = '\0';
      if (take > 1 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) {
        double value = strtod(number, &end);
        if (end == number || (take == sizeof(number) - 1 && (size_t)(end-number) == take && take < remaining)) { js->tok = TOK_ERR; break; }
        js->tval = tov(value);
        TOK(TOK_NUMBER, (jsoff_t)(end-number));
      }
      size_t i = 0, j = 0;
      bool seen_dot = false, seen_exp = false, invalid_separator = false;
      while (i < take) {
        unsigned char ch = (unsigned char) number[i];
        if (is_digit(ch)) { normalized[j++] = (char)ch; i++; continue; }
        if (ch == '_') {
          if (i == 0 || !is_digit((unsigned char)number[i-1]) || i+1 >= take || !is_digit((unsigned char)number[i+1])) { invalid_separator = true; break; }
          i++; continue;
        }
        if (ch == '.' && !seen_dot && !seen_exp) { seen_dot = true; normalized[j++] = (char)ch; i++; continue; }
        if ((ch == 'e' || ch == 'E') && !seen_exp) {
          seen_exp = true; normalized[j++] = (char)ch; i++;
          if (i < take && (number[i] == '+' || number[i] == '-')) normalized[j++] = number[i++];
          continue;
        }
        break;
      }
      if (invalid_separator) { js->tok = TOK_ERR; js->tlen = js->clen - js->toff; break; }
      normalized[j] = '\0';
      double value = strtod(normalized, &end);
      if (end == normalized || (size_t)(end-normalized) != j || (i < take && is_ident_continue((unsigned char)number[i])) || (take == sizeof(number)-1 && i == take && take < remaining)) { js->tok = TOK_ERR; break; }
      js->tval = tov(value);
      TOK(TOK_NUMBER, (jsoff_t)i);
    }
    default: js->tok = parseident(buf, js->clen - js->toff, &js->tlen); break;
  }  // clang-format on
  js->pos = js->toff + js->tlen;
  // printf("NEXT: %d %d [%.*s]\n", js->tok, js->pos, (int) js->tlen, buf);
  return js->tok;
}

static inline uint8_t lookahead(struct js *js) {
  uint8_t old = js->tok, tok = 0;
  jsoff_t pos = js->pos;
  js->consumed = 1;
  tok = v7_next_token(js);
  js->pos = pos, js->tok = old;
  return tok;
}

static void mkscope(struct js *js) {
  assert((js->flags & F_NOEXEC) == 0);
  jsoff_t prev = (jsoff_t) vdata(js->scope);
  js->scope = mkobj(js, prev);
  // printf("ENTER SCOPE %u, prev %u\n", (jsoff_t) vdata(js->scope), prev);
}

static void delscope(struct js *js) {
  js->scope = upper(js, js->scope);
  // printf("EXIT  SCOPE %u\n", (jsoff_t) vdata(js->scope));
}
static jsoff_t lkp(struct js *, jsval_t, const char *, size_t);
static jsval_t prop_invoke(struct js *, jsval_t, jsval_t, const jsval_t *, int);
/* `var` targets the nearest function scope (or the script/global scope),
 * while `let` and `const` stay in the current lexical block. */
static jsval_t dm_var_scope(struct js *js) {
  jsval_t scope=js->scope;
  for(unsigned depth=0;depth<128 && vtype(scope)==T_OBJ;depth++) {
    jsoff_t marker=lkp(js,scope,"__dm_var_scope",14);
    if(marker) return scope;
    jsval_t parent=upper(js,scope);
    if(vdata(parent)==vdata(scope) || vdata(scope)==0) return scope;
    scope=parent;
  }
  return js->scope;
}

/* Focused automatic semicolon insertion at statement boundaries. */
static bool dm_linebreak_before(const char *source, size_t token_start) {
  size_t i = token_start;
  bool linebreak = false;
  while (i > 0) {
    char ch = source[i - 1];
    if (ch == '\n' || ch == '\r') { linebreak = true; i--; continue; }
    if (ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f') { i--; continue; }
    break;
  }
  return linebreak;
}

static jsval_t js_block(struct js *js, bool create_scope) {
  jsval_t res = js_mkundef();
  if (create_scope) mkscope(js);  // Enter new scope
  js->consumed = 1;
  // jsoff_t pos = js->pos;
  while (v7_next_token(js) != TOK_EOF && v7_next_token(js) != TOK_RBRACE && !is_err(res)) {
    uint8_t t = js->tok;
    res = js_stmt(js);
    if (!is_err(res) && t != TOK_LBRACE && t != TOK_IF && t != TOK_WHILE && t != TOK_TRY &&
        t != TOK_FUNC && t != TOK_CLASS && js->tok != TOK_SEMICOLON &&
        v7_next_token(js) != TOK_RBRACE && v7_next_token(js) != TOK_EOF &&
        !dm_linebreak_before(js->code, (size_t) js->toff)) {
      res = js_mkerr(js, "; expected");
      break;
    }
  }
  // printf("BLOCKEND %s\n", js_str(js, res));
  if (create_scope) delscope(js);  // Exit scope
  return res;
}
// Small exact-key property lookup cache. Invalidated on mutation and GC.
static uint32_t v7_key_hash(const char *s, size_t n) { uint32_t h=2166136261U; for(size_t i=0;i<n;i++)h=(h^(unsigned char)s[i])*16777619U; return h?h:1U; }
static void v7_lkp_cache_clear(struct js *js) { memset(js->lkp_cache,0,sizeof(js->lkp_cache)); js->lkp_cache_next=0; }
// Seach for property in a single object
static jsoff_t lkp(struct js *js, jsval_t obj, const char *buf, size_t len) {
  uint32_t hash=v7_key_hash(buf,len); jsoff_t object_off=(jsoff_t)vdata(obj);
  for(unsigned i=0;i<32;i++){jsoff_t po=js->lkp_cache[i].property;if(po&&js->lkp_cache[i].object==object_off&&js->lkp_cache[i].hash==hash&&po<js->brk){jsoff_t ko=js->lkp_cache[i].key;if(ko<js->brk){jsoff_t kn=offtolen(loadoff(js,ko));const char*kp=(const char*)&js->mem[ko+sizeof(jsoff_t)];if(kn==len&&!memcmp(kp,buf,len)){jsval_t cv=loadval(js,po+sizeof(jsoff_t)*2);if(vtype(cv)!=T_HOLE){if(js->flags&F_LOOP)js->nitrous_passes++;return po;}}}}}
  jsoff_t off = loadoff(js, (jsoff_t) vdata(obj)) & ~3U;  // Load first prop off
  // printf("LKP: %lu %u [%.*s]\n", vdata(obj), off, (int) len, buf);
  while (off < js->brk && off != 0) {  // Iterate over props
    jsoff_t koff = loadoff(js, (jsoff_t) (off + sizeof(off)));
    jsoff_t klen = (loadoff(js, koff) >> 2) - 1;
    const char *p = (char *) &js->mem[koff + sizeof(koff)];
    // printf("  %u %u[%.*s]\n", off, (int) klen, (int) klen, p);
    if (streq(buf, len, p, klen)) {
      jsval_t value = loadval(js, off + sizeof(jsoff_t) * 2);
      /* A tombstone hides older duplicate properties but remains reclaimable by GC. */
      if(vtype(value)==T_HOLE)return 0;
      unsigned slot=js->lkp_cache_next++%32U;
      js->lkp_cache[slot].object=object_off;js->lkp_cache[slot].property=off;
      js->lkp_cache[slot].key=koff;js->lkp_cache[slot].hash=hash;
      return off;
    }  // Found !
    off = loadoff(js, off) & ~3U;              // Load next prop offset
  }
  return 0;  // Not found
}

/* Search an object's prototype chain. Object entities store their parent
 * object offset in the second word; zero means no prototype. */
static jsoff_t lkp_chain(struct js *js, jsval_t obj, const char *buf, size_t len) {
  unsigned depth = 0;
  while (vtype(obj) == T_OBJ && depth++ < 128) {    jsoff_t off = lkp(js, obj, buf, len);
    if (off != 0) return off;
    jsoff_t parent = loadoff(js, (jsoff_t) (vdata(obj) + sizeof(jsoff_t)));
    if (parent == 0 || parent == vdata(obj)) break;
    obj = mkval(T_OBJ, parent);
  }
  return 0;
}

// Lookup variable in the scope chain
static jsval_t lookup(struct js *js, const char *buf, size_t len) {
  if (js->flags & F_NOEXEC) return 0;
  for (jsval_t scope = js->scope;;) {
    jsoff_t off = lkp(js, scope, buf, len);
    if (off != 0) return mkval(T_PROP, off);
    if (vdata(scope) == 0) break;
    scope =
        mkval(T_OBJ, loadoff(js, (jsoff_t) (vdata(scope) + sizeof(jsoff_t))));
  }
  return js_mkerr(js, "'%.*s' not found", (int) len, buf);
}

static jsval_t resolveprop(struct js *js, jsval_t v) {
  if (vtype(v) != T_PROP) return v;
  jsoff_t off = (jsoff_t)vdata(v);
  jsval_t getter = prop_getter(js, off);
  if (prop_flags(js, off) & V7_PROP_ACCESSOR) {
    if (!(js->flags & F_NOEXEC) && (vtype(getter) == T_FUNC || vtype(getter) == T_CFUNC)) {
      jsval_t result = prop_invoke(js, getter, mkval(T_OBJ, prop_owner(js, off)), NULL, 0);
      return is_err(result) ? result : resolveprop(js, result);
    }
    return js_mkundef();
  }
  jsval_t value = loadval(js, off + sizeof(jsoff_t) * 2);
  if (vtype(value) == T_HOLE) return js_mkundef();
  return resolveprop(js, value);
}

static void dm_array_assignment_sync(struct js *js, jsval_t lhs, jsval_t val);
static jsval_t assign(struct js *js, jsval_t lhs, jsval_t val) {
  if (vtype(lhs) != T_PROP) return js_mkerr(js, "invalid assignment target");
  jsoff_t off = (jsoff_t)(vdata(lhs) & ~3U);
  uint32_t flags = prop_flags(js, off);
  jsval_t setter = prop_setter(js, off);
  if (flags & V7_PROP_ACCESSOR) {
    if (vtype(setter) == T_FUNC || vtype(setter) == T_CFUNC) {
      jsval_t result = prop_invoke(js, setter, mkval(T_OBJ, prop_owner(js, off)), &val, 1);
      return is_err(result) ? result : lhs;
    }
    return js->strict_mode ? js_mkerr(js, "cannot set property without a setter") : lhs;
  }
  if (!(flags & V7_PROP_WRITABLE))
    return js->strict_mode ? js_mkerr(js, "cannot assign to read-only property") : lhs;
  dm_array_assignment_sync(js, lhs, val);
  saveval(js, off + sizeof(jsoff_t) * 2, val);
  return lhs;
}

static jsval_t do_assign_op(struct js *js, uint8_t op, jsval_t l, jsval_t r) {
  uint8_t binary;
  switch (op) {
    case TOK_PLUS_ASSIGN: binary = TOK_PLUS; break;
    case TOK_MINUS_ASSIGN: binary = TOK_MINUS; break;
    case TOK_MUL_ASSIGN: binary = TOK_MUL; break;
    case TOK_DIV_ASSIGN: binary = TOK_DIV; break;
    case TOK_REM_ASSIGN: binary = TOK_REM; break;
    case TOK_SHL_ASSIGN: binary = TOK_SHL; break;
    case TOK_SHR_ASSIGN: binary = TOK_SHR; break;
    case TOK_ZSHR_ASSIGN: binary = TOK_ZSHR; break;
    case TOK_AND_ASSIGN: binary = TOK_AND; break;
    case TOK_XOR_ASSIGN: binary = TOK_XOR; break;
    case TOK_OR_ASSIGN: binary = TOK_OR; break;
    case TOK_EXP_ASSIGN: binary = TOK_EXP; break;
    case TOK_NULLISH_ASSIGN: { jsval_t current = resolveprop(js, l); return (vtype(current) == T_NULL || vtype(current) == T_UNDEF) ? assign(js, l, r) : current; }
    default: return js_mkerr(js, "unknown compound assignment %u", (unsigned) op);
  }
  jsval_t res = do_op(js, binary, resolveprop(js, l), r);
  if (is_err(res)) return res;
  return assign(js, l, res);
}

static jsval_t do_string_op(struct js *js, uint8_t op, jsval_t l, jsval_t r) {
  jsoff_t n1, off1 = vstr(js, l, &n1);
  jsoff_t n2, off2 = vstr(js, r, &n2);
  if (op == TOK_PLUS) {
    jsval_t res = js_mkstr(js, NULL, n1 + n2);
    // printf("STRPLUS %u %u %u %u [%.*s] [%.*s]\n", n1, off1, n2, off2, (int)
    // n1,
    //       &js->mem[off1], (int) n2, &js->mem[off2]);
    if (vtype(res) == T_STR) {
      jsoff_t n, off = vstr(js, res, &n);
      memmove(&js->mem[off], &js->mem[off1], n1);
      memmove(&js->mem[off + n1], &js->mem[off2], n2);
    }
    return res;
  } else if (op == TOK_EQ) {
    bool eq = n1 == n2 && memcmp(&js->mem[off1], &js->mem[off2], n1) == 0;
    return mkval(T_BOOL, eq ? 1 : 0);
  } else if (op == TOK_NE) {
    bool eq = n1 == n2 && memcmp(&js->mem[off1], &js->mem[off2], n1) == 0;
    return mkval(T_BOOL, eq ? 0 : 1);
  } else if (op == TOK_LT || op == TOK_LE || op == TOK_GT || op == TOK_GE) {
    size_t common = n1 < n2 ? n1 : n2;
    int cmp = memcmp(&js->mem[off1], &js->mem[off2], common);
    if (cmp == 0) cmp = n1 < n2 ? -1 : n1 > n2 ? 1 : 0;
    if (op == TOK_LT) return mkval(T_BOOL, cmp < 0);
    if (op == TOK_LE) return mkval(T_BOOL, cmp <= 0);
    if (op == TOK_GT) return mkval(T_BOOL, cmp > 0);
    return mkval(T_BOOL, cmp >= 0);
  } else {
    return js_mkerr(js, "bad str op");
  }
}

static jsval_t prop_value(struct js *js, jsval_t obj, const char *key) {
  jsoff_t off;
  if (vtype(obj) == T_FUNC) {
    int fi = function_index(js, (jsoff_t)vdata(obj));
    if (fi < 0) return js_mkundef();
    obj = mkval(T_OBJ, js->function_props[fi]);
  }
  if (vtype(obj) != T_OBJ) return js_mkundef();
  off = lkp_chain(js, obj, key, strlen(key));
  return off ? resolveprop(js, mkval(T_PROP, off)) : js_mkundef();
}

static void prop_set(struct js *js, jsval_t obj, const char *key, jsval_t value) {
  if (vtype(obj) == T_FUNC) {
    for (unsigned i = 0; i < js->closure_count; i++) if (js->closure_fn[i] == (jsoff_t) vdata(obj)) { obj = mkval(T_OBJ, js->function_props[i]); break; }
  }
  jsoff_t off = vtype(obj) == T_OBJ ? lkp(js, obj, key, strlen(key)) : 0;
  if (off) {
    jsval_t setter = prop_setter(js, off);
    uint32_t flags = prop_flags(js, off);
    if (flags & V7_PROP_ACCESSOR) {
      if (vtype(setter) == T_FUNC || vtype(setter) == T_CFUNC) (void)prop_invoke(js, setter, obj, &value, 1);
    } else if (flags & V7_PROP_WRITABLE) {
      saveval(js, off + sizeof(jsoff_t) * 2, value);
    }
  } else if (vtype(obj) == T_OBJ) {
    (void) setprop(js, obj, js_mkstr(js, key, strlen(key)), value);
  }
}

static bool is_promise(struct js *js, jsval_t v) {
  return vtype(v) == T_OBJ && vtype(prop_value(js, v, "__dm_promise")) == T_BOOL && vdata(prop_value(js, v, "__dm_promise")) != 0;
}

static jsval_t promise_make(struct js *js, int state, jsval_t value) {
  jsval_t p = mkobj(js, js->promise_proto);
  if (is_err(p)) return p;
  prop_set(js, p, "__dm_promise", js_mktrue());
  prop_set(js, p, "__dm_state", tov((double) state));
  prop_set(js, p, "__dm_value", value);
  prop_set(js, p, "__dm_head", js_mkundef());
  prop_set(js, p, "__dm_tail", js_mkundef());
  return p;
}

static int promise_state(struct js *js, jsval_t p) {
  jsval_t v = prop_value(js, p, "__dm_state");
  return vtype(v) == T_NUM ? (int) tod(v) : 0;
}

static jsval_t promise_value(struct js *js, jsval_t p) {
  return prop_value(js, p, "__dm_value");
}

static jsval_t promise_handler_make(struct js *js, jsval_t on_ok,
                                    jsval_t on_err, jsval_t child) {
  jsval_t h = mkobj(js, 0);
  prop_set(js, h, "__dm_ok", on_ok);
  prop_set(js, h, "__dm_err", on_err);
  prop_set(js, h, "__dm_child", child);
  prop_set(js, h, "__dm_next", js_mkundef());
  prop_set(js, h, "__dm_job_state", tov(0));
  return h;
}

static void queue_microtask(struct js *js, jsval_t handler, jsval_t value,
                            int state) {
  unsigned at;
  if (js->job_count >= JS_JOB_MAX) { (void) js_mkerr(js, "microtask queue full"); return; }
  at = (js->job_head + js->job_count) % JS_JOB_MAX;
  prop_set(js, handler, "__dm_job_state", tov((double) state));
  js->job_cb[at] = handler;
  js->job_val[at] = value;
  js->job_child[at] = prop_value(js, handler, "__dm_child");
  js->job_count++;
}

static void promise_append_handler(struct js *js, jsval_t p, jsval_t h) {
  jsval_t tail = prop_value(js, p, "__dm_tail");
  if (vtype(tail) == T_OBJ) {
    prop_set(js, tail, "__dm_next", h);
  } else {
    prop_set(js, p, "__dm_head", h);
  }
  prop_set(js, p, "__dm_tail", h);
}

static void promise_settle(struct js *js, jsval_t p, int state, jsval_t value) {
  jsval_t h;
  if (!is_promise(js, p) || promise_state(js, p) != 0) return;
  if (state == 1 && is_promise(js, value)) {
    int vs = promise_state(js, value);
    if (vdata(value) == vdata(p)) {
      state = -1;
      value = js_mkstr(js, "TypeError: promise cannot resolve to itself", 42);
    } else if (vs == 0) {
      h = promise_handler_make(js, js_mkundef(), js_mkundef(), p);
      promise_append_handler(js, value, h);
      return;
    } else {
      state = vs;
      value = promise_value(js, value);
    }
  }
  prop_set(js, p, "__dm_state", tov((double) state));
  prop_set(js, p, "__dm_value", value);
  h = prop_value(js, p, "__dm_head");
  prop_set(js, p, "__dm_head", js_mkundef());
  prop_set(js, p, "__dm_tail", js_mkundef());
  while (vtype(h) == T_OBJ) {
    jsval_t next_h = prop_value(js, h, "__dm_next");
    queue_microtask(js, h, value, state);
    h = next_h;
  }
}

static jsval_t promise_pending_native(struct js *js, jsval_t *args, int nargs) {
  (void) args; (void) nargs;
  return promise_make(js, 0, js_mkundef());
}
static jsval_t promise_resolve_into_native(struct js *js, jsval_t *args, int nargs) {
  if (nargs >= 2) promise_settle(js, args[0], 1, args[1]);
  return js_mkundef();
}

static jsval_t promise_reject_into_native(struct js *js, jsval_t *args, int nargs) {
  if (nargs >= 2) promise_settle(js, args[0], -1, args[1]);
  return js_mkundef();
}

static jsval_t promise_resolve_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t v = nargs ? args[0] : js_mkundef();
  jsval_t p;
  if (is_promise(js, v)) return v;
  p = promise_make(js, 0, js_mkundef());
  promise_settle(js, p, 1, v);
  return p;
}

static jsval_t promise_reject_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t p = promise_make(js, 0, js_mkundef());
  promise_settle(js, p, -1, nargs ? args[0] : js_mkundef());
  return p;
}

static jsval_t promise_then_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t p = js->this_val;
  jsval_t child = promise_make(js, 0, js_mkundef());
  jsval_t on_ok = nargs > 0 && vtype(args[0]) == T_FUNC ? args[0] : js_mkundef();
  jsval_t on_err = nargs > 1 && vtype(args[1]) == T_FUNC ? args[1] : js_mkundef();
  jsval_t h = promise_handler_make(js, on_ok, on_err, child);
  int state = promise_state(js, p);
  if (state == 0) promise_append_handler(js, p, h);
  else queue_microtask(js, h, promise_value(js, p), state);
  return child;
}

static jsval_t promise_catch_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t p = js->this_val;
  jsval_t child = promise_make(js, 0, js_mkundef());
  jsval_t on_err = nargs > 0 && vtype(args[0]) == T_FUNC ? args[0] : js_mkundef();
  jsval_t h = promise_handler_make(js, js_mkundef(), on_err, child);
  int state = promise_state(js, p);
  if (state == 0) promise_append_handler(js, p, h);
  else queue_microtask(js, h, promise_value(js, p), state);
  return child;
}

static jsval_t promise_finally_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t p = js->this_val;
  jsval_t child, h;
  int state;
  if (!nargs || (vtype(args[0]) != T_FUNC && vtype(args[0]) != T_CFUNC))
    return promise_then_native(js, NULL, 0);
  child = promise_make(js, 0, js_mkundef());
  h = promise_handler_make(js, js_mkundef(), js_mkundef(), child);
  prop_set(js, h, "__dm_finally", js_mktrue());
  prop_set(js, h, "__dm_finally_cb", args[0]);
  state = promise_state(js, p);
  if (state == 0) promise_append_handler(js, p, h);
  else queue_microtask(js, h, promise_value(js, p), state);
  return child;
}

static void drain_microtasks(struct js *js) {
  unsigned budget = 4096;
  if (js->draining_jobs) return;
  js->draining_jobs = 1;
  while (js->job_count && budget--) {
    unsigned at = js->job_head;
    jsval_t h = js->job_cb[at], value = js->job_val[at];
    jsval_t child = js->job_child[at];
    int state = vtype(prop_value(js, h, "__dm_job_state")) == T_NUM
                  ? (int) tod(prop_value(js, h, "__dm_job_state")) : 0;
    jsval_t callback;
    char src[] = "__dm_job_cb(__dm_job_val);";
    js->job_cb[at] = js_mkundef(); js->job_val[at] = js_mkundef(); js->job_child[at] = js_mkundef();
    js->job_head = (js->job_head + 1) % JS_JOB_MAX; js->job_count--;
    if (vtype(prop_value(js, h, "__dm_finally")) == T_BOOL && vdata(prop_value(js, h, "__dm_finally")) != 0) {
      prop_set(js, mkval(T_OBJ, 0), "__dm_job_cb", prop_value(js, h, "__dm_finally_cb"));
      prop_set(js, mkval(T_OBJ, 0), "__dm_job_val", js_mkundef());
      js->has_thrown = 0;
      jsval_t final_result = js_eval(js, "__dm_job_cb();", sizeof("__dm_job_cb();") - 1);
      if (is_err(final_result)) {
        jsval_t reason = js->has_thrown ? js->thrown : js_mkstr(js, js->errmsg, strlen(js->errmsg));
        js->has_thrown = 0;
        promise_settle(js, child, -1, reason);
      } else {
        promise_settle(js, child, state, value);
      }
      continue;
    }
    if ((state == 1 && vtype(prop_value(js, h, "__dm_ok")) != T_FUNC && vtype(prop_value(js, h, "__dm_ok")) != T_CFUNC) ||
        (state == -1 && vtype(prop_value(js, h, "__dm_err")) != T_FUNC && vtype(prop_value(js, h, "__dm_err")) != T_CFUNC)) {
      promise_settle(js, child, state, value);
      continue;
    }
    callback = state == 1 ? prop_value(js, h, "__dm_ok") : prop_value(js, h, "__dm_err");
    prop_set(js, mkval(T_OBJ, 0), "__dm_job_cb", callback);
    prop_set(js, mkval(T_OBJ, 0), "__dm_job_val", value);
    js->has_thrown = 0;
    jsval_t result = js_eval(js, src, sizeof(src) - 1);
    if (is_err(result)) {
      jsval_t reason = js->has_thrown ? js->thrown : js_mkstr(js, js->errmsg, strlen(js->errmsg));
      js->has_thrown = 0;
      promise_settle(js, child, -1, reason);
    } else {
      promise_settle(js, child, 1, result);
    }
  }
  js->draining_jobs = 0;
}

static uint64_t runtime_now_ms(void) {
  struct timespec ts;
  if (timespec_get(&ts, TIME_UTC) == TIME_UTC)
    return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
  return (uint64_t) clock() * 1000ULL / (uint64_t) CLOCKS_PER_SEC;
}

/* Kinetic scheduler hook. Embedded boards may define DM_PLATFORM_SLEEP_MS. */
#ifndef DM_PLATFORM_SLEEP_MS
/* ISO C fallback: embedded hosts may override this with low-power sleep. */
static void dm_platform_sleep(uint64_t ms) {
  clock_t start = clock();
  if (start == (clock_t) -1) return;
  double ticks = ((double) ms * (double) CLOCKS_PER_SEC) / 1000.0;
  while ((double) (clock() - start) < ticks) { /* portable busy wait */ }
}
#define DM_PLATFORM_SLEEP_MS(ms) dm_platform_sleep(ms)
#endif
static void runtime_sleep_ms(struct js*js,uint64_t ms){if(!ms)return;if(js)js->kinetic_sleep_ms+=ms;
#if defined(__EMSCRIPTEN__)
(void)ms;
#else
DM_PLATFORM_SLEEP_MS(ms);
#endif
}

static struct js_timer *timer_find(struct js *js, uint32_t id) {
  for (unsigned i = 0; i < JS_TIMER_MAX; i++)
    if (js->timers[i].active && js->timers[i].id == id) return &js->timers[i];
  return NULL;
}

static uint32_t timer_add(struct js *js, jsval_t callback, jsval_t *args,
                          int argc, uint64_t delay, int repeat) {
  struct js_timer *t = NULL;
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (!js->timers[i].active) { t = &js->timers[i]; break; }
  if (t == NULL) return 0;
  memset(t, 0, sizeof(*t));
  t->active = 1; t->repeat = (uint8_t) (repeat != 0);
  t->id = ++js->next_timer_id;
  if (t->id == 0) t->id = ++js->next_timer_id;
  t->due_ms = runtime_now_ms() + delay;
  t->interval_ms = delay;
  t->callback = callback;
  t->argc = argc > JS_TIMER_ARGS ? JS_TIMER_ARGS : argc;
  for (int i = 0; i < t->argc; i++) t->args[i] = args[i];
  t->payload = js_mkundef(); t->promise = js_mkundef();
  return t->id;
}

static void runtime_call_timer(struct js *js, struct js_timer *t) {
  char source[256];
  size_t used = 0;
  const char *saved_code = js->code;
  jsoff_t saved_clen = js->clen, saved_pos = js->pos, saved_toff = js->toff, saved_tlen = js->tlen;
  jsval_t saved_scope = js->scope, saved_this = js->this_val, saved_thrown = js->thrown;
  void *saved_cstk = js->cstk;
  uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags, saved_has_thrown = js->has_thrown;
  prop_set(js, mkval(T_OBJ, 0), "__dm_timer_cb", t->callback);
  for (int i = 0; i < t->argc; i++) {
    char name[20];
    (void) snprintf(name, sizeof(name), "__dm_timer_a%d", i);
    prop_set(js, mkval(T_OBJ, 0), name, t->args[i]);
  }
  used += (size_t) snprintf(source + used, sizeof(source) - used, "__dm_timer_cb(");
  for (int i = 0; i < t->argc && used < sizeof(source); i++) {
    char name[20];
    (void) snprintf(name, sizeof(name), "__dm_timer_a%d", i);
    used += (size_t) snprintf(source + used, sizeof(source) - used, "%s%s", i ? "," : "", name);
  }
  (void) snprintf(source + (used < sizeof(source) ? used : sizeof(source) - 1),
                  used < sizeof(source) ? sizeof(source) - used : 1, ");");
  js->has_thrown = 0;
  jsval_t result = js_eval(js, source, strlen(source));
  if (is_err(result)) {
    const char *message = js->has_thrown ? js_str(js, js->thrown) : js_str(js, result);
    fprintf(stderr, "Uncaught timer callback: %s\n", message ? message : "error");
    js->has_thrown = 0;
  }
  prop_set(js, mkval(T_OBJ, 0), "__dm_timer_cb", js_mkundef());
  for (int i = 0; i < t->argc; i++) {
    char name[20]; (void) snprintf(name, sizeof(name), "__dm_timer_a%d", i);
    prop_set(js, mkval(T_OBJ, 0), name, js_mkundef());
  }
  /* A timer callback can run while an outer eval is suspended inside await.
   * Restore the parser/interpreter frame so the suspended statement continues. */
  js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos;
  js->toff = saved_toff; js->tlen = saved_tlen; js->tok = saved_tok;
  js->consumed = saved_consumed; js->flags = saved_flags;
  js->scope = saved_scope; js->this_val = saved_this; js->cstk = saved_cstk;
  js->has_thrown = saved_has_thrown; js->thrown = saved_thrown;
}

static int run_one_event(struct js *js, int wait_for_timer) {
  struct js_timer *soonest = NULL;
  uint64_t now = runtime_now_ms();
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) {
    struct js_timer *t = &js->timers[i];
    if (t->active && (soonest == NULL || t->due_ms < soonest->due_ms || (t->due_ms == soonest->due_ms && t->id < soonest->id))) soonest = t;
  }
  if (soonest == NULL) return 0;
  if (soonest->due_ms > now) {
#if defined(__EMSCRIPTEN__)
    if (wait_for_timer) return 0;
#else
    if (!wait_for_timer) return 0;
    runtime_sleep_ms(js, soonest->due_ms - now);
    now = runtime_now_ms();
    if (soonest->due_ms > now) return 1;
#endif
  }
  /* Re-select after sleeping because another timer may now be earlier. */
  soonest = NULL;
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) {
    struct js_timer *t = &js->timers[i];
    if (t->active && t->due_ms <= now && (soonest == NULL || t->due_ms < soonest->due_ms || (t->due_ms == soonest->due_ms && t->id < soonest->id))) soonest = t;
  }
  if (soonest == NULL) return 1;
  if (soonest->kind == 3) { /* delay(ms) Promise */
    promise_settle(js, soonest->promise, 1, js_mkundef());
    soonest->active = 0;
  } else if (soonest->kind == 1) { /* readFile task */
    char pathbuf[1024]; size_t plen = 0;
    char *path = js_getstr(js, soonest->payload, &plen);
    if (path == NULL || plen >= sizeof(pathbuf)) {
      promise_settle(js, soonest->promise, -1, js_mkstr(js, "readFile: invalid path", 22));
    } else {
      FILE *f; long end; char *data;
      memcpy(pathbuf, path, plen); pathbuf[plen] = '\0';
      f = fopen(pathbuf, "rb");
      if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f) fclose(f);
        jsval_t msg = js_mkstr(js, "readFile: could not read file", 29);
        promise_settle(js, soonest->promise, -1, msg);
      } else {
        data = (char *) malloc((size_t) end + 1U);
        if (data == NULL || fread(data, 1, (size_t) end, f) != (size_t) end) {
          free(data); fclose(f);
          promise_settle(js, soonest->promise, -1, js_mkstr(js, "readFile: out of memory or read error", 37));
        } else {
          fclose(f); data[end] = '\0';
          promise_settle(js, soonest->promise, 1, js_mkstr(js, data, (size_t) end));
          free(data);
        }
      }
    }
    soonest->active = 0;
  } else if (soonest->kind == 2) { /* writeFile task */
    char pathbuf[1024]; size_t plen = 0, dlen = 0;
    char *path = js_getstr(js, soonest->payload, &plen);
    char *data = js_getstr(js, soonest->args[0], &dlen);
    FILE *f = NULL;
    if (path != NULL && plen < sizeof(pathbuf) && data != NULL) {
      memcpy(pathbuf, path, plen); pathbuf[plen] = '\0';
      f = fopen(pathbuf, "wb");
    }
    if (f == NULL) {
      promise_settle(js, soonest->promise, -1, js_mkstr(js, "writeFile: could not write file", 31));
    } else {
      int write_ok = fwrite(data, 1, dlen, f) == dlen;
      int close_ok = fclose(f) == 0;
      if (!write_ok || !close_ok) promise_settle(js, soonest->promise, -1, js_mkstr(js, "writeFile: could not write file", 31));
      else promise_settle(js, soonest->promise, 1, js_mktrue());
    }
    soonest->active = 0;
  } else {
    struct js_timer snapshot = *soonest;
    if (soonest->repeat) soonest->due_ms = now + (soonest->interval_ms ? soonest->interval_ms : 1);
    else soonest->active = 0;
    runtime_call_timer(js, &snapshot);
  }
  drain_microtasks(js);
  return 1;
}

static void run_event_loop(struct js *js, int wait_for_timer) {
  unsigned budget = 100000;
  if (js->running_loop) return;
  js->running_loop = 1;
  while (budget--) {
    int any = 0;
    drain_microtasks(js);
    for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (js->timers[i].active) { any = 1; break; }
    if (!any) break;
    if (!run_one_event(js, wait_for_timer)) break;
  }
  js->running_loop = 0;
}

static jsval_t queue_microtask_native(struct js *js, jsval_t *args, int nargs) {
  jsval_t callback, h;
  if (nargs < 1 || (vtype(args[0]) != T_FUNC && vtype(args[0]) != T_CFUNC))
    return js_mkerr(js, "queueMicrotask requires a callback");
  callback = args[0];
  h = promise_handler_make(js, callback, js_mkundef(), js_mkundef());
  queue_microtask(js, h, js_mkundef(), 1);
  return js_mkundef();
}

static jsval_t set_timeout_native(struct js *js, jsval_t *args, int nargs) {
  uint64_t delay = 0; uint32_t id;
  if (nargs < 1 || (vtype(args[0]) != T_FUNC && vtype(args[0]) != T_CFUNC))
    return js_mkerr(js, "setTimeout requires a callback");
  if (nargs > 1 && vtype(args[1]) == T_NUM && tod(args[1]) > 0) {
    double d = tod(args[1]); delay = d > 86400000.0 ? 86400000ULL : (uint64_t) d;
  }
  id = timer_add(js, args[0], args + (nargs > 2 ? 2 : nargs), nargs > 2 ? nargs - 2 : 0, delay, 0);
  return id ? tov((double) id) : js_mkerr(js, "timer queue full");
}

static jsval_t set_interval_native(struct js *js, jsval_t *args, int nargs) {
  uint64_t delay = 1; uint32_t id;
  if (nargs < 1 || (vtype(args[0]) != T_FUNC && vtype(args[0]) != T_CFUNC))
    return js_mkerr(js, "setInterval requires a callback");
  if (nargs > 1 && vtype(args[1]) == T_NUM && tod(args[1]) >= 0) {
    double d = tod(args[1]); delay = d > 86400000.0 ? 86400000ULL : (uint64_t) d;
    if (delay == 0) delay = 1;
  }
  id = timer_add(js, args[0], args + (nargs > 2 ? 2 : nargs), nargs > 2 ? nargs - 2 : 0, delay, 1);
  return id ? tov((double) id) : js_mkerr(js, "timer queue full");
}

static jsval_t clear_timer_native(struct js *js, jsval_t *args, int nargs) {
  if (nargs > 0 && vtype(args[0]) == T_NUM) {
    struct js_timer *t = timer_find(js, (uint32_t) tod(args[0]));
    if (t != NULL) t->active = 0;
  }
  return js_mkundef();
}

static jsval_t delay_native(struct js *js, jsval_t *args, int nargs) {
  struct js_timer *t = NULL;
  jsval_t p = promise_make(js, 0, js_mkundef());
  uint64_t delay = 0;
  if (nargs > 0 && vtype(args[0]) == T_NUM && tod(args[0]) > 0) {
    double d = tod(args[0]); delay = d > 86400000.0 ? 86400000ULL : (uint64_t) d;
  }
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (!js->timers[i].active) { t = &js->timers[i]; break; }
  if (t == NULL) return js_mkerr(js, "event queue full");
  memset(t, 0, sizeof(*t)); t->active = 1; t->kind = 3;
  t->due_ms = runtime_now_ms() + delay; t->promise = p;
  t->callback = js_mkundef(); t->payload = js_mkundef();
  t->id = ++js->next_timer_id; if (t->id == 0) t->id = ++js->next_timer_id;
  return p;
}

static jsval_t read_file_native(struct js *js, jsval_t *args, int nargs) {
  uint32_t id; jsval_t p; struct js_timer *t = NULL;
  if (nargs < 1 || vtype(args[0]) != T_STR) return js_mkerr(js, "readFile requires a path string");
  p = promise_make(js, 0, js_mkundef());
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (!js->timers[i].active) { t = &js->timers[i]; break; }
  if (t == NULL) return js_mkerr(js, "event queue full");
  memset(t, 0, sizeof(*t)); t->active = 1; t->kind = 1; t->due_ms = runtime_now_ms();
  t->payload = args[0]; t->promise = p; t->callback = js_mkundef();
  id = ++js->next_timer_id; if (id == 0) id = ++js->next_timer_id; t->id = id;
  return p;
}

static jsval_t write_file_native(struct js *js, jsval_t *args, int nargs) {
  uint32_t id; jsval_t p; struct js_timer *t = NULL;
  if (nargs < 2 || vtype(args[0]) != T_STR || vtype(args[1]) != T_STR)
    return js_mkerr(js, "writeFile requires path and text strings");
  p = promise_make(js, 0, js_mkundef());
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (!js->timers[i].active) { t = &js->timers[i]; break; }
  if (t == NULL) return js_mkerr(js, "event queue full");
  memset(t, 0, sizeof(*t)); t->active = 1; t->kind = 2; t->due_ms = runtime_now_ms();
  t->payload = args[0]; t->promise = p; t->callback = js_mkundef(); t->argc = 1; t->args[0] = args[1];
  id = ++js->next_timer_id; if (id == 0) id = ++js->next_timer_id; t->id = id;
  return p;
}

static jsval_t require_native(struct js *js, jsval_t *args, int nargs) {
#if defined(__EMSCRIPTEN__)
  (void) args; (void) nargs;
  return js_mkerr(js, "require() filesystem modules are unavailable in this browser build");
#else
  char path[1024]; size_t plen = 0; char *path_src; FILE *f; long end; char *source;
  jsval_t global = mkval(T_OBJ, 0), cache, cached, module, exports;
  jsval_t old_module = prop_value(js, global, "module");
  jsval_t old_exports = prop_value(js, global, "exports");
  const char *saved_code = js->code; jsoff_t saved_clen = js->clen, saved_pos = js->pos, saved_toff = js->toff, saved_tlen = js->tlen;
  jsval_t saved_scope = js->scope, saved_this = js->this_val, saved_thrown = js->thrown;
  void *saved_cstk = js->cstk;
  uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags, saved_has_thrown = js->has_thrown;
  if (nargs < 1 || vtype(args[0]) != T_STR) return js_mkerr(js, "require() expects a module path string");
  path_src = js_getstr(js, args[0], &plen);
  if (path_src == NULL || plen == 0 || plen >= sizeof(path)) return js_mkerr(js, "require() invalid module path");
  memcpy(path, path_src, plen); path[plen] = '\0';
  cache = prop_value(js, global, "__dm_modules");
  if (vtype(cache) != T_OBJ) { cache = mkobj(js, 0); prop_set(js, global, "__dm_modules", cache); }
  cached = prop_value(js, cache, path);
  if (vtype(cached) == T_OBJ) return prop_value(js, cached, "exports");
  f = fopen(path, "rb");
  if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
    if (f != NULL) fclose(f);
    return js_mkerr(js, "require() could not open module");
  }
  source = (char *) malloc((size_t) end + 1U);
  if (source == NULL || fread(source, 1, (size_t) end, f) != (size_t) end) {
    free(source); fclose(f); return js_mkerr(js, "require() failed reading module");
  }
  fclose(f); source[end] = '\0';
  module = mkobj(js, 0); exports = mkobj(js, 0);
  prop_set(js, module, "exports", exports);
  /* Cache before execution to permit simple cyclic requires. */
  prop_set(js, cache, path, module);
  prop_set(js, global, "module", module); prop_set(js, global, "exports", exports);
  js->has_thrown = 0;
  jsval_t result = js_eval(js, source, (size_t) end);
  free(source);
  if (is_err(result)) {
    prop_set(js, cache, path, js_mkundef());
    prop_set(js, global, "module", old_module); prop_set(js, global, "exports", old_exports);
    js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos; js->toff = saved_toff; js->tlen = saved_tlen;
    js->tok = saved_tok; js->consumed = saved_consumed; js->flags = saved_flags; js->scope = saved_scope; js->this_val = saved_this; js->cstk = saved_cstk;
    js->has_thrown = saved_has_thrown; js->thrown = saved_thrown;
    return js_mkerr(js, "require() module evaluation failed");
  }
  exports = prop_value(js, module, "exports");
  prop_set(js, global, "module", old_module); prop_set(js, global, "exports", old_exports);
  js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos; js->toff = saved_toff; js->tlen = saved_tlen;
  js->tok = saved_tok; js->consumed = saved_consumed; js->flags = saved_flags; js->scope = saved_scope; js->this_val = saved_this; js->cstk = saved_cstk;
  js->has_thrown = saved_has_thrown; js->thrown = saved_thrown;
  return exports;
#endif
}

static jsval_t console_write(struct js *js, jsval_t *args, int nargs, FILE *stream) {
  for (int i = 0; i < nargs; i++) {
    char buf[1024]; size_t n = 0; char *str;
    if (i) fputc(' ', stream);
    if (vtype(args[i]) == T_STR && (str = js_getstr(js, args[i], &n)) != NULL) {
      fwrite(str, 1, n, stream);
    } else {
      n = tostr(js, args[i], buf, sizeof(buf));
      fwrite(buf, 1, n < sizeof(buf) ? n : sizeof(buf) - 1, stream);
    }
  }
  fputc('\n', stream); fflush(stream);
  return js_mkundef();
}

static jsval_t console_log_native(struct js *js, jsval_t *args, int nargs) {
  return console_write(js, args, nargs, stdout);
}
static jsval_t console_error_native(struct js *js, jsval_t *args, int nargs) {
  return console_write(js, args, nargs, stderr);
}

static int function_index(struct js *js, jsoff_t fn_entity) {
  for (unsigned i = 0; i < js->closure_count; i++)
    if (js->closure_fn[i] == fn_entity) return (int) i;
  return -1;
}

static int dm_array(struct js *js, jsval_t v);
static unsigned long dm_array_length(struct js *js, jsval_t a);
static int console_label(struct js *js, jsval_t *args, int nargs, char out[64]) {
  char tmp[128]; size_t n;
  if (nargs < 1) { memcpy(out, "default", 8); return 1; }
  n = tostr(js, args[0], tmp, sizeof(tmp)); if (n >= 63) n = 63;
  memcpy(out, tmp, n); out[n] = '\0'; return 1;
}
static jsval_t console_time_native(struct js *js, jsval_t *args, int nargs) {
  char label[64]; int slot = -1; console_label(js, args, nargs, label);
  for (int i = 0; i < 16; i++) {
    if (js->console_timer_active[i] && strcmp(js->console_timer_labels[i], label) == 0) { slot = i; break; }
    if (!js->console_timer_active[i] && slot < 0) slot = i;
  }
  if (slot < 0) return js_mkerr(js, "console.time: timer limit reached");
  memcpy(js->console_timer_labels[slot], label, strlen(label) + 1U);
  js->console_timer_started[slot] = runtime_now_ms(); js->console_timer_active[slot] = 1;
  return js_mkundef();
}
static jsval_t console_time_end_native(struct js *js, jsval_t *args, int nargs) {
  char label[64]; console_label(js, args, nargs, label);
  for (int i = 0; i < 16; i++) if (js->console_timer_active[i] && strcmp(js->console_timer_labels[i], label) == 0) {
    uint64_t elapsed = runtime_now_ms() - js->console_timer_started[i];
    fprintf(stdout, "%s: %llu ms\n", label, (unsigned long long) elapsed);
    fflush(stdout); js->console_timer_active[i] = 0; return js_mkundef();
  }
  fprintf(stderr, "console.timeEnd: timer not found: %s\n", label); return js_mkundef();
}
static jsval_t console_table_native(struct js *js, jsval_t *args, int nargs) {
  if (nargs < 1) { fputs("(empty table)\n", stdout); return js_mkundef(); }
  fputs("index\tvalue\n", stdout);
  if (dm_array(js, args[0])) {
    unsigned long length = dm_array_length(js, args[0]);
    for (unsigned long i = 0; i < length; i++) {
      char key[32], value[1024]; (void) snprintf(key, sizeof(key), "%lu", i);
      size_t n = tostr(js, prop_value(js, args[0], key), value, sizeof(value));
      fprintf(stdout, "%lu\t%.*s\n", i, (int)(n < sizeof(value) ? n : sizeof(value)-1U), value);
    }
  } else { char value[1024]; size_t n=tostr(js,args[0],value,sizeof(value)); fprintf(stdout,"0\t%.*s\n",(int)(n<sizeof(value)?n:sizeof(value)-1U),value); }
  fflush(stdout); return js_mkundef();
}
static jsval_t console_trace_native(struct js *js, jsval_t *args, int nargs) {
  fputs("Trace: ", stderr); return console_write(js, args, nargs, stderr);
}
static int dm_string_this(struct js *js, const char **s, size_t *n) { *s = js_getstr(js, js->this_val, n); return *s != NULL; }
static size_t dm_string_pos(jsval_t v, size_t len, int negative_from_end) {
  if (vtype(v) != T_NUM) return 0; double d = trunc(tod(v));
  if (d < 0 && negative_from_end) d += (double) len; else if (d < 0) d = 0;
  if (d < 0) d = 0; if (d > (double) len) d = (double) len; return (size_t) d;
}
static size_t dm_string_find(const char *s, size_t len, const char *q, size_t qlen, size_t start) {
  if (qlen == 0) return start <= len ? start : len; if (start > len || qlen > len - start) return SIZE_MAX;
  for (size_t i = start; i + qlen <= len; i++) if (memcmp(s + i, q, qlen) == 0) return i; return SIZE_MAX;
}
static jsval_t dm_string_includes(struct js *js, jsval_t *a, int n) {
  const char *s,*q; size_t len,qlen; if(!dm_string_this(js,&s,&len)||n<1||vtype(a[0])!=T_STR)return js_mkerr(js,"String.includes requires a string search value");
  q=js_getstr(js,a[0],&qlen);size_t pos=n>1?dm_string_pos(a[1],len,0):0;return mkval(T_BOOL,dm_string_find(s,len,q,qlen,pos)!=SIZE_MAX);
}
static jsval_t dm_string_indexof(struct js *js, jsval_t *a, int n) {
  const char *s,*q;size_t len,qlen;if(!dm_string_this(js,&s,&len)||n<1||vtype(a[0])!=T_STR)return js_mkerr(js,"String.indexOf requires a string search value");q=js_getstr(js,a[0],&qlen);size_t pos=n>1?dm_string_pos(a[1],len,0):0;size_t at=dm_string_find(s,len,q,qlen,pos);return tov(at==SIZE_MAX?-1.0:(double)at);
}
static jsval_t dm_string_startswith(struct js *js, jsval_t *a, int n) {
  const char *s,*q;size_t len,qlen;if(!dm_string_this(js,&s,&len)||n<1||vtype(a[0])!=T_STR)return js_mkerr(js,"String.startsWith requires a string");q=js_getstr(js,a[0],&qlen);size_t pos=n>1?dm_string_pos(a[1],len,0):0;return mkval(T_BOOL,qlen<=len-pos&&memcmp(s+pos,q,qlen)==0);
}
static jsval_t dm_string_endswith(struct js *js, jsval_t *a, int n) {
  const char *s,*q;size_t len,qlen;if(!dm_string_this(js,&s,&len)||n<1||vtype(a[0])!=T_STR)return js_mkerr(js,"String.endsWith requires a string");q=js_getstr(js,a[0],&qlen);size_t end=n>1?dm_string_pos(a[1],len,0):len;return mkval(T_BOOL,qlen<=end&&memcmp(s+end-qlen,q,qlen)==0);
}
static jsval_t dm_string_slice(struct js *js, jsval_t *a, int n) {
  const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.slice receiver is not a string");size_t start=n>0?dm_string_pos(a[0],len,1):0,end=n>1?dm_string_pos(a[1],len,1):len;if(end<start)end=start;return js_mkstr(js,s+start,end-start);
}
static jsval_t dm_string_substring(struct js *js, jsval_t *a, int n) {
  const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.substring receiver is not a string");size_t start=n>0?dm_string_pos(a[0],len,0):0,end=n>1?dm_string_pos(a[1],len,0):len;if(start>end){size_t t=start;start=end;end=t;}return js_mkstr(js,s+start,end-start);
}
static int dm_string_space(unsigned char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
static jsval_t dm_string_trim(struct js *js, jsval_t *a, int n) {
  (void)a;(void)n;const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.trim receiver is not a string");size_t start=0,end=len;while(start<end&&dm_string_space((unsigned char)s[start]))start++;while(end>start&&dm_string_space((unsigned char)s[end-1]))end--;return js_mkstr(js,s+start,end-start);
}
static jsval_t dm_string_case(struct js *js, int to_upper) {
  const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String case method receiver is not a string");char *buf=(char*)malloc(len?len:1);if(!buf)return js_mkerr(js,"out of memory in String case method");for(size_t i=0;i<len;i++){unsigned char c=(unsigned char)s[i];buf[i]=(char)(to_upper?(c>='a'&&c<='z'?c-32:c):(c>='A'&&c<='Z'?c+32:c));}jsval_t out=js_mkstr(js,buf,len);free(buf);return out;
}
static jsval_t dm_string_upper(struct js *js, jsval_t *a, int n) {(void)a;(void)n;return dm_string_case(js,1);}
static jsval_t dm_string_lower(struct js *js, jsval_t *a, int n) {(void)a;(void)n;return dm_string_case(js,0);}
static jsval_t dm_string_charat(struct js *js, jsval_t *a, int n) {
  const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.charAt receiver is not a string");double d=n>0&&vtype(a[0])==T_NUM?trunc(tod(a[0])):0;if(d<0||d>=(double)len)return js_mkstr(js,"",0);return js_mkstr(js,s+(size_t)d,1);
}
static jsval_t dm_string_repeat(struct js *js, jsval_t *a, int n) {
  const char *s;size_t len;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.repeat receiver is not a string");double d=n>0&&vtype(a[0])==T_NUM?trunc(tod(a[0])):0;if(d<0||d>1000000)return js_mkerr(js,"invalid String.repeat count");size_t count=(size_t)d;if(len&&count>16000000U/len)return js_mkerr(js,"repeated string too large");size_t total=len*count;char *buf=(char*)malloc(total?total:1);if(!buf)return js_mkerr(js,"out of memory in String.repeat");for(size_t i=0;i<count;i++)if(len)memcpy(buf+i*len,s,len);jsval_t out=js_mkstr(js,buf,total);free(buf);return out;
}
static jsval_t dm_string_pad(struct js *js, jsval_t *a, int n, int left) {
  const char *s = NULL, *fill = " "; size_t len = 0, fill_len = 1;
  if (!dm_string_this(js, &s, &len)) return js_mkerr(js, "String pad receiver is not a string");
  if (n < 1) return js_mkstr(js, s, len);
  double d = dm_to_number(js, a[0]);
  if (isnan(d) || d <= (double)len) return js_mkstr(js, s, len);
  if (d > 1000000.0) return js_mkerr(js, "String padding too large");
  size_t target = (size_t)floor(d);
  if (n > 1 && vtype(a[1]) != T_UNDEF) {
    if (vtype(a[1]) == T_STR) fill = js_getstr(js, a[1], &fill_len);
    else { char tmp[128]; fill_len = tostr(js, a[1], tmp, sizeof(tmp)); fill = js_getstr(js, js_mkstr(js, tmp, fill_len), &fill_len); }
  }
  if (fill == NULL || fill_len == 0) return js_mkstr(js, s, len);
  size_t pad = target - len;
  if (target > 16000000U) return js_mkerr(js, "padded string too large");
  char *buf = (char *)malloc(target + 1U);
  if (!buf) return js_mkerr(js, "out of memory in String.pad");
  size_t offset = 0;
  if (!left) { if (len) memcpy(buf, s, len); offset = len; }
  for (size_t i = 0; i < pad; i++) buf[offset + i] = fill[i % fill_len];
  if (left && len) memcpy(buf + pad, s, len);
  buf[target] = '\0';
  jsval_t result = js_mkstr(js, buf, target);
  free(buf);
  return result;
}
static jsval_t dm_string_padstart(struct js *js, jsval_t *a, int n) { return dm_string_pad(js, a, n, 1); }
static jsval_t dm_string_padend(struct js *js, jsval_t *a, int n) { return dm_string_pad(js, a, n, 0); }
static jsval_t dm_string_trimside(struct js *js, int left) {
  const char *s; size_t len;
  if (!dm_string_this(js, &s, &len)) return js_mkerr(js, "String trim receiver is not a string");
  size_t start = 0, end = len;
  if (left) while (start < end && dm_string_space((unsigned char)s[start])) start++;
  else while (end > start && dm_string_space((unsigned char)s[end - 1])) end--;
  return js_mkstr(js, s + start, end - start);
}
static jsval_t dm_string_trimstart(struct js *js, jsval_t *a, int n) { (void)a; (void)n; return dm_string_trimside(js, 1); }
static jsval_t dm_string_trimend(struct js *js, jsval_t *a, int n) { (void)a; (void)n; return dm_string_trimside(js, 0); }
static jsval_t dm_string_replace(struct js *js, jsval_t *a, int n) {
  const char *s, *search, *replacement; size_t len, slen, rlen;
  if (!dm_string_this(js, &s, &len) || n < 2 || vtype(a[0]) != T_STR || vtype(a[1]) != T_STR)
    return js_mkerr(js, "String.replace currently requires string search and replacement values");
  search = js_getstr(js, a[0], &slen);
  replacement = js_getstr(js, a[1], &rlen);
  size_t at = dm_string_find(s, len, search, slen, 0);
  if (at == SIZE_MAX) return js_mkstr(js, s, len);
  if (len - slen > 16000000U || rlen > 16000000U - (len - slen))
    return js_mkerr(js, "replacement result too large");
  size_t outlen = len - slen + rlen;
  char *buf = (char *)malloc(outlen + 1U);
  if (!buf) return js_mkerr(js, "out of memory in String.replace");
  if (at) memcpy(buf, s, at);
  if (rlen) memcpy(buf + at, replacement, rlen);
  if (len > at + slen) memcpy(buf + at + rlen, s + at + slen, len - at - slen);
  buf[outlen] = '\0';
  jsval_t result = js_mkstr(js, buf, outlen);
  free(buf);
  return result;
}
static jsval_t dm_string_replaceall(struct js *js, jsval_t *a, int n) {
  const char *s, *search, *replacement;
  size_t len, slen, rlen, count = 0, start = 0, outlen;
  char *buf;
  if (!dm_string_this(js, &s, &len) || n < 2 ||
      vtype(a[0]) != T_STR || vtype(a[1]) != T_STR)
    return js_mkerr(js, "String.replaceAll currently requires string search and replacement values");
  search = js_getstr(js, a[0], &slen);
  replacement = js_getstr(js, a[1], &rlen);
  if (len > 16000000U) return js_mkerr(js, "replacement result too large");

  if (slen == 0) {
    if (len >= 16000000U || (rlen && len + 1U > (16000000U - len) / rlen))
      return js_mkerr(js, "replacement result too large");
    outlen = len + (len + 1U) * rlen;
    buf = (char *) malloc(outlen + 1U);
    if (!buf) return js_mkerr(js, "out of memory in String.replaceAll");
    size_t pos = 0;
    for (size_t i = 0; i <= len; i++) {
      if (rlen) { memcpy(buf + pos, replacement, rlen); pos += rlen; }
      if (i < len) buf[pos++] = s[i];
    }
    buf[pos] = '\0';
    jsval_t result = js_mkstr(js, buf, pos);
    free(buf);
    return result;
  }

  while (start <= len) {
    size_t at = dm_string_find(s, len, search, slen, start);
    if (at == SIZE_MAX) break;
    count++;
    start = at + slen;
  }
  if (rlen >= slen) {
    size_t extra = rlen - slen;
    if (extra && count > (16000000U - len) / extra)
      return js_mkerr(js, "replacement result too large");
    outlen = len + count * extra;
  } else {
    outlen = len - count * (slen - rlen);
  }
  if (outlen > 16000000U) return js_mkerr(js, "replacement result too large");
  buf = (char *) malloc(outlen + 1U);
  if (!buf) return js_mkerr(js, "out of memory in String.replaceAll");

  size_t read_pos = 0, write_pos = 0;
  while (read_pos <= len) {
    size_t at = dm_string_find(s, len, search, slen, read_pos);
    if (at == SIZE_MAX) break;
    if (at > read_pos) {
      memcpy(buf + write_pos, s + read_pos, at - read_pos);
      write_pos += at - read_pos;
    }
    if (rlen) {
      memcpy(buf + write_pos, replacement, rlen);
      write_pos += rlen;
    }
    read_pos = at + slen;
  }
  if (len > read_pos) {
    memcpy(buf + write_pos, s + read_pos, len - read_pos);
    write_pos += len - read_pos;
  }
  buf[write_pos] = '\0';
  jsval_t result = js_mkstr(js, buf, write_pos);
  free(buf);
  return result;
}

static jsval_t dm_string_at(struct js *js, jsval_t *a, int n) {
  const char *s; size_t len;
  if (!dm_string_this(js, &s, &len)) return js_mkerr(js, "String.at receiver is not a string");
  if (n < 1) return js_mkundef();
  double d = dm_to_number(js, a[0]);
  if (isnan(d)) d = 0.0;
  if (!isfinite(d)) return js_mkundef();
  d = d < 0.0 ? ceil(d) : floor(d);
  if (d < 0.0) d += (double)len;
  if (d < 0.0 || d >= (double)len) return js_mkundef();
  return js_mkstr(js, s + (size_t)d, 1);
}
static jsval_t dm_string_charcodeat(struct js *js, jsval_t *a, int n) {
  const char *s; size_t len;
  if (!dm_string_this(js, &s, &len)) return js_mkerr(js, "String.charCodeAt receiver is not a string");
  double d = n > 0 ? dm_to_number(js, a[0]) : 0.0;
  if (isnan(d)) d = 0.0;
  if (!isfinite(d)) return tov(NAN);
  d = d < 0.0 ? ceil(d) : floor(d);
  if (d < 0.0 || d >= (double)len) return tov(NAN);
  return tov((unsigned char)s[(size_t)d]);
}
static jsval_t dm_string_split(struct js *js, jsval_t *a, int n) {
  const char *s,*q;size_t len,qlen;if(!dm_string_this(js,&s,&len))return js_mkerr(js,"String.split receiver is not a string");jsval_t out=dm_array_new(js);if(n<1||vtype(a[0])==T_UNDEF){dm_array_store(js,out,0,js_mkstr(js,s,len));return out;}if(vtype(a[0])!=T_STR)return js_mkerr(js,"String.split requires a string separator");q=js_getstr(js,a[0],&qlen);size_t limit=n>1&&vtype(a[1])==T_NUM?dm_string_pos(a[1],1000000,0):1000000;if(limit==0)return out;size_t start=0,used=0;if(qlen==0){while(start<len&&used<limit){dm_array_store(js,out,used++,js_mkstr(js,s+start,1));start++;}return out;}while(used<limit){size_t at=dm_string_find(s,len,q,qlen,start);if(at==SIZE_MAX)break;dm_array_store(js,out,used++,js_mkstr(js,s+start,at-start));start=at+qlen;}if(used<limit)dm_array_store(js,out,used++,js_mkstr(js,s+start,len-start));return out;
}

static jsval_t do_dot_op(struct js *js, jsval_t l, jsval_t r) {
  const char *ptr = NULL;
  size_t keylen = 0;
  char keybuf[64];
  if (vtype(r) == T_CODEREF) {
    ptr = (char *) &js->code[coderefoff(r)];
    keylen = codereflen(r);
  } else if (vtype(r) == T_STR) {
    ptr = js_getstr(js, r, &keylen);
  } else if (vtype(r) == T_NUM || vtype(r) == T_BOOL || vtype(r) == T_NULL || vtype(r) == T_UNDEF) {
    keylen = tostr(js, r, keybuf, sizeof(keybuf));
    ptr = keybuf;
  } else {
    return js_mkerr(js, "invalid property key");
  }
  // Handle stringvalue.length
  if (vtype(l) == T_STR && streq(ptr, keylen, "length", 6)) {
    return tov(offtolen(loadoff(js, (jsoff_t) vdata(l))));
  }
  if (vtype(l) == T_STR) {
    if (streq(ptr,keylen,"includes",8)) return js_mkfun(dm_string_includes);
    if (streq(ptr,keylen,"indexOf",7)) return js_mkfun(dm_string_indexof);
    if (streq(ptr,keylen,"startsWith",10)) return js_mkfun(dm_string_startswith);
    if (streq(ptr,keylen,"endsWith",8)) return js_mkfun(dm_string_endswith);
    if (streq(ptr,keylen,"slice",5)) return js_mkfun(dm_string_slice);
    if (streq(ptr,keylen,"substring",9)) return js_mkfun(dm_string_substring);
    if (streq(ptr,keylen,"trim",4)) return js_mkfun(dm_string_trim);
    if (streq(ptr,keylen,"toUpperCase",11)) return js_mkfun(dm_string_upper);    if (streq(ptr,keylen,"toLowerCase",11)) return js_mkfun(dm_string_lower);
    if (streq(ptr,keylen,"charAt",6)) return js_mkfun(dm_string_charat);
    if (streq(ptr,keylen,"at",2)) return js_mkfun(dm_string_at);
    if (streq(ptr,keylen,"charCodeAt",10)) return js_mkfun(dm_string_charcodeat);
    if (streq(ptr,keylen,"repeat",6)) return js_mkfun(dm_string_repeat);
    if (streq(ptr,keylen,"padStart",8)) return js_mkfun(dm_string_padstart);
    if (streq(ptr,keylen,"padEnd",6)) return js_mkfun(dm_string_padend);
    if (streq(ptr,keylen,"trimStart",9)) return js_mkfun(dm_string_trimstart);
    if (streq(ptr,keylen,"trimEnd",7)) return js_mkfun(dm_string_trimend);
    if (streq(ptr,keylen,"replace",7)) return js_mkfun(dm_string_replace);
    if (streq(ptr,keylen,"replaceAll",10)) return js_mkfun(dm_string_replaceall);
    if (streq(ptr,keylen,"split",5)) return js_mkfun(dm_string_split);
    if (streq(ptr,keylen,"match",5)) return js_mkfun(dm_string_match);
    if (streq(ptr,keylen,"search",6)) return js_mkfun(dm_string_search);
    if (keylen > 0 && keylen < 16) {
      unsigned long index = 0; int numeric = 1;
      for (size_t i = 0; i < keylen; i++) {
        if (ptr[i] < '0' || ptr[i] > '9' || (i == 0 && keylen > 1 && ptr[i] == '0')) { numeric = 0; break; }
        index = index * 10UL + (unsigned long)(ptr[i] - '0');
      }
      if (numeric) {
        size_t string_len = 0; const char *string_data = js_getstr(js, l, &string_len);
        if (string_data && index < string_len) return js_mkstr(js, string_data + index, 1);
      }
    }
    return js_mkundef();
  }
  if (vtype(l) == T_FUNC) {
    int fi = function_index(js, (jsoff_t) vdata(l));
    if (fi < 0) return js_mkundef();
    return do_dot_op(js, mkval(T_OBJ, js->function_props[fi]), r);
  }
  if (vtype(l) != T_OBJ) return js_mkerr(js, "lookup in non-obj");
  if (dm_array(js, l) && keylen > 0 && keylen < 16) {
    unsigned long idx = 0; int numeric = 1;
    for (size_t q = 0; q < keylen; q++) {
      if (ptr[q] < '0' || ptr[q] > '9' || (q == 0 && keylen > 1 && ptr[q] == '0')) { numeric = 0; break; }
      idx = idx * 10UL + (unsigned long)(ptr[q] - '0');
      if (idx > 1000000UL) { numeric = 0; break; }
    }
    (void)numeric; (void)idx; /* Keep an l-value property for out-of-range writes. */
  }
  jsoff_t off = lkp_chain(js, l, ptr, keylen);
  if (off == 0) {
    if (js->flags & F_NOEXEC) return js_mkundef();    jsval_t key = js_mkstr(js, ptr, keylen);
    jsval_t added = setprop(js, l, key, js_mkundef());
    if (is_err(added)) return added;
    off = lkp(js, l, ptr, keylen);
  }
  return off == 0 ? js_mkundef() : mkval(T_PROP, off);
}

static jsval_t js_call_params(struct js *js) {
  jsoff_t pos = js->pos;
  uint8_t flags = js->flags;
  js->flags |= F_NOEXEC;
  js->consumed = 1;
  for (bool comma = false; v7_next_token(js) != TOK_EOF; comma = true) {
    if (!comma && v7_next_token(js) == TOK_RPAREN) break;
    js_expr(js);
    if (v7_next_token(js) == TOK_RPAREN) break;
    EXPECT(TOK_COMMA, js->flags = flags);
  }
  EXPECT(TOK_RPAREN, js->flags = flags);
  js->flags = flags;
  return mkcoderef(pos, js->pos - pos - js->tlen);
}

static void reverse(jsval_t *args, int nargs) {
  for (int i = 0; i < nargs / 2; i++) {
    jsval_t tmp = args[i];
    args[i] = args[nargs - i - 1], args[nargs - i - 1] = tmp;
  }
}

// Call native C function
static jsval_t call_c(struct js *js,
                      jsval_t (*fn)(struct js *, jsval_t *, int)) {
  int argc = 0;
  while (js->pos < js->clen) {
    if (v7_next_token(js) == TOK_RPAREN) break;
    jsval_t arg = resolveprop(js, js_expr(js));
    if (js->brk + sizeof(arg) > js->size) return js_mkerr(js, "call oom");
    js->size -= (jsoff_t) sizeof(arg);
    memcpy(&js->mem[js->size], &arg, sizeof(arg));
    argc++;
    // printf("  arg %d -> %s\n", argc, js_str(js, arg));
    if (v7_next_token(js) == TOK_COMMA) js->consumed = 1;
  }
  reverse((jsval_t *) &js->mem[js->size], argc);
  jsval_t res = fn(js, (jsval_t *) &js->mem[js->size], argc);
  setlwm(js);
  js->size += (jsoff_t) sizeof(jsval_t) * (jsoff_t) argc;  // Restore stack
  return res;
}

/* Locate the end of a default-parameter expression without treating commas
 * or closing parentheses inside nested expressions and quoted strings as
 * parameter separators. */
static jsoff_t function_param_default_end(const char *fn, jsoff_t len, jsoff_t start) {
  unsigned paren = 0, bracket = 0, brace = 0;
  char quote = 0;
  for (jsoff_t i = start; i < len; i++) {
    char c = fn[i];
    if (quote) {
      if (c == '\\') { if (i + 1 < len) i++; continue; }
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
    if (i + 1 < len && c == '/' && fn[i + 1] == '/') {
      while (i < len && fn[i] != '\n') i++;
      continue;
    }
    if (i + 1 < len && c == '/' && fn[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(fn[i] == '*' && fn[i + 1] == '/')) i++;
      if (i + 1 < len) i++;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')') { if (paren == 0 && bracket == 0 && brace == 0) return i; if (paren) paren--; }
    else if (c == '[') bracket++;
    else if (c == ']') { if (bracket) bracket--; }
    else if (c == '{') brace++;
    else if (c == '}') { if (brace) brace--; }
    else if (c == ',' && paren == 0 && bracket == 0 && brace == 0) return i;
  }
  return len;
}

// Call JS function. 'fn' looks like this: "(a,b) { return a + b; }"
static jsval_t call_js(struct js *js, const char *fn, jsoff_t fnlen) {
  jsoff_t fnpos = 1;
  if (js->call_scope_depth >= 128) return js_mkerr(js, "function call depth exceeded");
  unsigned call_slot = js->call_scope_depth++;
  uint8_t previous_strict = js->strict_mode;
  uint8_t function_strict = 0;
  jsoff_t function_entity = (jsoff_t)((const uint8_t *)fn - js->mem - sizeof(jsoff_t));
  for (unsigned fi = 0; fi < js->closure_count; fi++) {
    if (js->closure_fn[fi] == function_entity) { function_strict = js->function_strict[fi]; break; }
  }
  js->strict_mode = function_strict;
  js->call_caller_scope[call_slot] = (jsoff_t) vdata(js->scope);
  js->call_function_scope[call_slot] = 0;
  jsoff_t fn_entity = (jsoff_t) ((const uint8_t *) fn - js->mem - sizeof(jsoff_t));
  for (unsigned i = 0; i < js->closure_count; i++) {
    if (js->closure_fn[i] == fn_entity) {
      js->scope = mkval(T_OBJ, js->closure_scope[i]);
      break;
    }
  }
  // printf("JSCALL [%.*s] -> %.*s\n", (int) js->clen, js->code, (int) fnlen,
  // fn);
  // printf("JSCALL, nogc %u [%.*s]\n", js->nogc, (int) fnlen, fn);
  mkscope(js);  // Create function call scope
  js->call_function_scope[call_slot] = (jsoff_t) vdata(js->scope);
  prop_set(js, js->scope, "__dm_var_scope", js_mktrue());
#define CALL_JS_FAIL(_value) do { jsval_t _failure = (_value); js->scope = mkval(T_OBJ, js->call_caller_scope[call_slot]); js->call_scope_depth = call_slot; js->strict_mode = previous_strict; return _failure; } while (0)
  // Arguments are evaluated in the caller's scope; defaults and the body use the captured function scope.
  while (fnpos < fnlen) {
    fnpos = skiptonext(fn, fnlen, fnpos);          // Skip to the identifier
    if (fnpos < fnlen && fn[fnpos] == ')') break;  // Closing paren? break!
    int is_rest = 0;
    if (fnpos + 2 < fnlen && fn[fnpos] == '.' && fn[fnpos+1] == '.' && fn[fnpos+2] == '.') {
      is_rest = 1; fnpos = skiptonext(fn, fnlen, fnpos + 3);
    }
    jsoff_t identlen = 0;
    uint8_t tok = TOK_IDENTIFIER;
    int pattern_kind = 0;
    jsoff_t pattern_end = 0;
    jsoff_t property_start[64], property_len[64], binding_start[64], binding_len[64], element_index[64];
    unsigned pattern_count = 0;
    if (fnpos < fnlen && (fn[fnpos] == '{' || fn[fnpos] == '[')) {
      char close = fn[fnpos] == '{' ? '}' : ']';
      pattern_kind = close == '}' ? 1 : 2;
      jsoff_t q = fnpos + 1;
      unsigned array_index = 0;
      while (q < fnlen) {
        q = skiptonext(fn, fnlen, q);
        if (q >= fnlen || fn[q] == close) break;
        if (pattern_kind == 2 && fn[q] == ',') { array_index++; q++; continue; }
        jsoff_t key_start = q, key_len = 0, bind_start = q, bind_len = 0;
        if (parseident(fn + q, fnlen - q, &key_len) != TOK_IDENTIFIER || key_len == 0)
          CALL_JS_FAIL(js_mkerr(js, "unsupported destructured function parameter"));
        q += key_len;
        if (pattern_kind == 1) {
          q = skiptonext(fn, fnlen, q);
          if (q < fnlen && fn[q] == ':') {
            q = skiptonext(fn, fnlen, q + 1);
            bind_start = q;
            if (q >= fnlen || parseident(fn + q, fnlen - q, &bind_len) != TOK_IDENTIFIER || bind_len == 0)
              CALL_JS_FAIL(js_mkerr(js, "binding identifier expected in destructured parameter"));
            q += bind_len;
          } else { bind_start = key_start; bind_len = key_len; }
        } else { bind_start = key_start; bind_len = key_len; }
        if (pattern_count >= 64) CALL_JS_FAIL(js_mkerr(js, "function destructuring limit exceeded"));
        property_start[pattern_count] = key_start; property_len[pattern_count] = key_len;
        binding_start[pattern_count] = bind_start; binding_len[pattern_count] = bind_len;
        element_index[pattern_count] = array_index++;
        pattern_count++;
        q = skiptonext(fn, fnlen, q);
        if (q < fnlen && fn[q] == ',') { q++; continue; }
        if (q < fnlen && fn[q] == close) break;
        CALL_JS_FAIL(js_mkerr(js, "comma or closing delimiter expected in destructured parameter"));
      }
      if (q >= fnlen || fn[q] != close) CALL_JS_FAIL(js_mkerr(js, "unterminated destructured function parameter"));
      pattern_end = q;
      identlen = pattern_end - fnpos + 1;
    } else {
      tok = parseident(&fn[fnpos], fnlen - fnpos, &identlen);
      if (tok != TOK_IDENTIFIER) break;
    }
    jsoff_t next_param = skiptonext(fn, fnlen, fnpos + identlen);
    jsoff_t default_start = 0, default_end = 0;
    if (!is_rest && next_param < fnlen && fn[next_param] == '=') {
      default_start = skiptonext(fn, fnlen, next_param + 1);
      default_end = function_param_default_end(fn, fnlen, default_start);
    }
    js->pos = skiptonext(js->code, js->clen, js->pos);
    js->consumed = 1;
    jsval_t v;
    if (is_rest) {
      v = dm_array_new(js);
      unsigned long ri = 0;
      while (v7_next_token(js) != TOK_RPAREN && v7_next_token(js) != TOK_EOF) {
        js->scope = mkval(T_OBJ, js->call_caller_scope[call_slot]);
        jsval_t item = resolveprop(js, js_expr(js));
        js->scope = mkval(T_OBJ, js->call_function_scope[call_slot]);
        if (is_err(item)) CALL_JS_FAIL(item);
        dm_array_store(js, v, ri++, item);
        if (v7_next_token(js) == TOK_COMMA) js->consumed = 1;
        else if (v7_next_token(js) != TOK_RPAREN && v7_next_token(js) != TOK_EOF) CALL_JS_FAIL(js_mkerr(js, "',' expected after rest argument"));
      }
      if (v7_next_token(js) == TOK_RPAREN) js->consumed = 1;
    } else {
      js->scope = mkval(T_OBJ, js->call_caller_scope[call_slot]);
      v = js->code[js->pos] == ')' ? js_mkundef() : js_expr(js);
      js->scope = mkval(T_OBJ, js->call_function_scope[call_slot]);
      if (is_err(v)) CALL_JS_FAIL(v);
      js->pos = skiptonext(js->code, js->clen, js->pos);
      if (js->pos < js->clen && js->code[js->pos] == ',') js->pos++;
    }
    if (default_end > default_start && vtype(resolveprop(js, v)) == T_UNDEF) {
      const char *saved_code = js->code;
      jsoff_t saved_clen = js->clen, saved_pos = js->pos;
      jsoff_t saved_toff = js->toff, saved_tlen = js->tlen;
      uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags;
      jsval_t default_value;
      size_t default_len = (size_t) (default_end - default_start);
      char *default_source = (char *) malloc(default_len + 3U);
      if (default_source == NULL) CALL_JS_FAIL(js_mkerr(js, "out of memory evaluating default parameter"));
      default_source[0] = '(';
      memcpy(default_source + 1, fn + default_start, default_len);
      default_source[default_len + 1U] = ')';
      default_source[default_len + 2U] = '\0';
      js->code = default_source;
      js->clen = (jsoff_t) (default_len + 2U);
      js->pos = 0; js->tok = TOK_ERR; js->consumed = 1;
      js->flags = F_CALL;
      default_value = js_eval(js, js->code, js->clen);
      free(default_source);
      js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos;
      js->toff = saved_toff; js->tlen = saved_tlen;
      js->tok = saved_tok; js->consumed = saved_consumed; js->flags = saved_flags;
      if (is_err(default_value)) CALL_JS_FAIL(default_value);
      v = resolveprop(js, default_value);
    }
    if (pattern_kind) {
      if (vtype(v) == T_NULL || vtype(v) == T_UNDEF)
        CALL_JS_FAIL(js_mkerr(js, "cannot destructure null or undefined parameter"));
      for (unsigned pi = 0; pi < pattern_count; pi++) {
        jsval_t item;
        if (pattern_kind == 1) {
          char key[128]; size_t n = property_len[pi];
          if (n >= sizeof(key)) CALL_JS_FAIL(js_mkerr(js, "destructured property name too long"));
          memcpy(key, fn + property_start[pi], n); key[n] = '\0';
          item = prop_value(js, v, key);
        } else {
          char key[32]; (void)snprintf(key, sizeof(key), "%lu", (unsigned long)element_index[pi]);
          item = prop_value(js, v, key);
        }
        if (lkp(js, js->scope, fn + binding_start[pi], binding_len[pi]) != 0)
          CALL_JS_FAIL(js_mkerr(js, "duplicate destructured parameter"));
        (void)setprop(js, js->scope, js_mkstr(js, fn + binding_start[pi], binding_len[pi]), item);
      }
    } else {
      if (js->strict_mode && lkp(js, js->scope, fn + fnpos, identlen) != 0)
        CALL_JS_FAIL(js_mkerr(js, "duplicate parameter name in strict function"));
      setprop(js, js->scope, js_mkstr(js, &fn[fnpos], identlen), v);
    }
    fnpos = default_end > default_start ? default_end : next_param;
    if (fnpos < fnlen && fn[fnpos] == ',') fnpos++;
  }
  if (fnpos < fnlen && fn[fnpos] == ')') fnpos++;  // Skip to the function body
  fnpos = skiptonext(fn, fnlen, fnpos);            // Up to the opening brace
  if (fnpos < fnlen && fn[fnpos] == '{') fnpos++;  // And skip the brace
  size_t n = fnlen - fnpos - 1U;  // Function code with stripped braces
  // printf("flags: %d, body: %zu [%.*s]\n", js->flags, n, (int) n, &fn[fnpos]);
  js->flags = F_CALL;                        // Mark we're in the function call
  jsval_t res = js_eval(js, &fn[fnpos], n);  // Call function, no GC
  if (!is_err(res) && !(js->flags & F_RETURN)) res = js_mkundef();  // No return
  js->scope = mkval(T_OBJ, js->call_caller_scope[call_slot]);
  js->call_scope_depth = call_slot;
  js->strict_mode = previous_strict;
#undef CALL_JS_FAIL
  // printf("  -> %d [%s], tok %d\n", js->flags, js_str(js, res), js->tok);
  return res;
}

static jsval_t do_call_op(struct js *js, jsval_t func, jsval_t args) {
  if (vtype(args) != T_CODEREF) return js_mkerr(js, "bad call");
  if (vtype(func) != T_FUNC && vtype(func) != T_CFUNC)
    return js_mkerr(js, "calling non-function");
  const char *code = js->code;             // Save current parser state
  jsoff_t clen = js->clen, pos = js->pos;  // code, position and code length
  js->code = &js->code[coderefoff(args)];  // Point parser to args
  js->clen = codereflen(args);             // Set args length
  js->pos = skiptonext(js->code, js->clen, 0);  // Skip to 1st arg
  uint8_t tok = js->tok, flags = js->flags, consumed = js->consumed; // Save parser state
  jsoff_t toff = js->toff, tlen = js->tlen, nogc = js->nogc;
  /* The argument source is a fresh token stream. Do not let the closing
   * parenthesis from the caller affect lexical decisions (notably /regex/). */
  js->tok = TOK_ERR; js->consumed = 1; js->toff = js->tlen = 0;
  jsval_t res = js_mkundef();
  if (vtype(func) == T_FUNC) {
    jsoff_t fnlen, fnoff = vstr(js, func, &fnlen);
    js->nogc = (jsoff_t) (fnoff - sizeof(jsoff_t));
    int call_fi = function_index(js, (jsoff_t) vdata(func));
    jsval_t call_old_this = js->this_val;
    if (call_fi >= 0 && js->function_arrow[call_fi]) js->this_val = js->function_this[call_fi];
    res = call_js(js, (const char *) (&js->mem[fnoff]), fnlen);
    js->this_val = call_old_this;
    { int fi = function_index(js, (jsoff_t) vdata(func));
      if (fi >= 0 && js->function_async[fi]) {
        if (is_err(res)) {
          jsval_t reason = js->has_thrown ? js->thrown : js_mkstr(js, js->errmsg, strlen(js->errmsg));
          js->has_thrown = 0; res = promise_make(js, -1, reason);
        } else if (!is_promise(js, res)) res = promise_make(js, 1, res);
      }
    }
  } else {
    res = call_c(js, (jsval_t(*)(struct js *, jsval_t *, int)) vdata(func));
  }
  js->code = code, js->clen = clen, js->pos = pos;  // Restore parser
  js->flags = flags, js->tok = tok, js->nogc = nogc;
  js->toff = toff; js->tlen = tlen; js->consumed = 1;
  (void)consumed;
  return res;
}

/* Invoke an accessor while preserving the suspended parser frame. Arguments
 * are temporarily exposed through non-enumerable internal globals so both
 * interpreted and native functions use the normal call machinery. */
static jsval_t prop_invoke(struct js *js, jsval_t fn, jsval_t owner, const jsval_t *args, int nargs) {
  const char *saved_code = js->code;
  jsoff_t saved_clen = js->clen, saved_pos = js->pos, saved_toff = js->toff, saved_tlen = js->tlen;
  uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags;
  jsval_t saved_this = js->this_val, result;
  char call_source[256]; size_t used = 0;
  if (nargs < 0 || nargs > 8) return js_mkerr(js, "accessor argument limit exceeded");
  for (int i = 0; i < nargs; i++) {
    char name[32]; (void)snprintf(name, sizeof(name), "__dm_prop_arg%d", i);
    prop_set(js, mkval(T_OBJ, 0), name, args[i]);
    int wrote = snprintf(call_source + used, sizeof(call_source) - used, "%s%s", i ? "," : "", name);
    if (wrote < 0 || (size_t)wrote >= sizeof(call_source) - used) return js_mkerr(js, "accessor call too large");
    used += (size_t)wrote;
  }
  js->code = call_source; js->clen = (jsoff_t)used; js->pos = 0;
  js->toff = 0; js->tlen = 0; js->tok = TOK_ERR; js->consumed = 1;
  js->this_val = owner;
  result = do_call_op(js, fn, mkcoderef(0, (jsoff_t)used));
  for (int i = 0; i < nargs; i++) { char name[32]; (void)snprintf(name, sizeof(name), "__dm_prop_arg%d", i); prop_set(js, mkval(T_OBJ, 0), name, mkval(T_HOLE, 0)); }
  js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos; js->toff = saved_toff; js->tlen = saved_tlen;
  js->tok = saved_tok; js->consumed = saved_consumed; js->flags = saved_flags; js->this_val = saved_this;
  return result;
}

// clang-format off
static uint32_t js_to_uint32(double value) {
  if (!isfinite(value) || value == 0.0) return 0;
  double n = fmod(trunc(value), 4294967296.0);
  if (n < 0.0) n += 4294967296.0;
  return (uint32_t) n;
}
static double js_to_int32_number(uint32_t value) {
  return value >= 0x80000000U ? (double) value - 4294967296.0 : (double) value;
}

static jsval_t do_op(struct js *js, uint8_t op, jsval_t lhs, jsval_t rhs) {
  if (js->flags & F_NOEXEC) return 0;
  jsval_t l = resolveprop(js, lhs), r = resolveprop(js, rhs);
  // printf("OP %d %d %d\n", op, vtype(lhs), vtype(r));
  setlwm(js);
  if (is_err(l)) return l;
  if (is_err(r)) return r;
  if ((is_assign(op) || op == TOK_POSTINC || op == TOK_POSTDEC || op == TOK_PREINC || op == TOK_PREDEC) && dm_const_lvalue(js, lhs))
    return js_mkerr(js, "Assignment to constant variable");
  if (op == TOK_PLUS && (vtype(l) == T_STR || vtype(r) == T_STR)) {
    char lb[512], rb[512]; size_t ln = 0, rn = 0;
    if (vtype(l) == T_STR) { const char *p = js_getstr(js, l, &ln); if (!p) ln = 0; if (ln >= sizeof(lb)) ln = sizeof(lb)-1; if (ln) memcpy(lb,p,ln); }
    else ln = tostr(js, l, lb, sizeof(lb)-1);
    if (vtype(r) == T_STR) { const char *p = js_getstr(js, r, &rn); if (!p) rn = 0; if (rn >= sizeof(rb)) rn = sizeof(rb)-1; if (rn) memcpy(rb,p,rn); }
    else rn = tostr(js, r, rb, sizeof(rb)-1);
    jsval_t ls = js_mkstr(js, lb, ln), rs = js_mkstr(js, rb, rn);
    if (is_err(ls) || is_err(rs)) return js_mkerr(js, "out of memory concatenating strings");
    return do_string_op(js, TOK_PLUS, ls, rs);
  }
  if (op == TOK_IN) {
    char keybuf[64]; const char *key = NULL; size_t keylen = 0;
    if (vtype(l) == T_STR) key = js_getstr(js, l, &keylen);
    else { keylen = tostr(js, l, keybuf, sizeof(keybuf)); if (keylen >= sizeof(keybuf)) keylen = sizeof(keybuf) - 1; key = keybuf; }
    if (vtype(r) == T_FUNC) { int fi = function_index(js, (jsoff_t) vdata(r)); if (fi < 0) return js_mkfalse(); r = mkval(T_OBJ, js->function_props[fi]); }
    if (vtype(r) != T_OBJ) return js_mkerr(js, "right side of 'in' must be an object");
    return mkval(T_BOOL, key != NULL && lkp_chain(js, r, key, keylen) != 0);
  }
  if (op == TOK_INSTANCEOF) {
    int fi = vtype(r) == T_FUNC ? function_index(js, (jsoff_t) vdata(r)) : -1;
    jsval_t proto_value = fi >= 0 ? function_prototype_value(js, r) : js_mkundef();
    jsoff_t target = vtype(proto_value) == T_OBJ ? (jsoff_t) vdata(proto_value) : 0;
    if (vtype(l) != T_OBJ || target == 0) return js_mkfalse();
    for (unsigned depth = 0; depth < 128 && vtype(l) == T_OBJ; depth++) {
      jsoff_t parent = loadoff(js, (jsoff_t) (vdata(l) + sizeof(jsoff_t)));
      if (parent == target) return js_mktrue();
      if (parent == 0 || parent == vdata(l)) break;
      l = mkval(T_OBJ, parent);
    }
    return js_mkfalse();
  }
  if (is_assign(op) && vtype(lhs) != T_PROP) return js_mkerr(js, "bad lhs");
  switch (op) {
    case TOK_TYPEOF: { const char *name = vtype(r) == T_NULL || vtype(r) == T_OBJ ? "object" : vtype(r) == T_CFUNC ? "function" : vtype(r) == T_HOLE ? "undefined" : typestr(vtype(r)); return js_mkstr(js, name, strlen(name)); }
    case TOK_CALL:    return do_call_op(js, l, r);
    case TOK_ASSIGN:  return assign(js, lhs, r);
    case TOK_POSTINC: {
      jsval_t updated;
      if (vtype(lhs) != T_PROP) return js_mkerr(js, "bad lhs for ++");
      updated = do_assign_op(js, TOK_PLUS_ASSIGN, lhs, tov(1));
      return is_err(updated) ? updated : l;
    }
    case TOK_POSTDEC: {
      jsval_t updated;
      if (vtype(lhs) != T_PROP) return js_mkerr(js, "bad lhs for --");
      updated = do_assign_op(js, TOK_MINUS_ASSIGN, lhs, tov(1));
      return is_err(updated) ? updated : l;
    }
    case TOK_PREINC: case TOK_PREDEC: {
      jsval_t updated;
      if (vtype(lhs) != T_PROP) return js_mkerr(js, "bad lhs for prefix increment/decrement");
      updated = do_assign_op(js, op == TOK_PREINC ? TOK_PLUS_ASSIGN : TOK_MINUS_ASSIGN, lhs, tov(1));
      return is_err(updated) ? updated : resolveprop(js, lhs);
    }
    case TOK_NOT:     return mkval(T_BOOL, !js_truthy(js, r));
  }
  if (is_assign(op))    return do_assign_op(js, op, lhs, r);
  if (op == TOK_EQ || op == TOK_NE || op == TOK_EQ2 || op == TOK_NE2) {
    bool loose = op == TOK_EQ2 || op == TOK_NE2;
    bool equal = false;
    uint8_t lt = vtype(l), rt = vtype(r);
    if (lt == rt) {
      if (lt == T_NUM) equal = tod(l) == tod(r); /* NaN remains unequal. */
      else if (lt == T_STR) {
        size_t ln = 0, rn = 0; const char *lp = js_getstr(js, l, &ln), *rp = js_getstr(js, r, &rn);
        equal = lp != NULL && rp != NULL && ln == rn && memcmp(lp, rp, ln) == 0;
      } else if (lt == T_NULL || lt == T_UNDEF) equal = true;
      else equal = vdata(l) == vdata(r);
    } else if (loose && ((lt == T_NULL && rt == T_UNDEF) || (lt == T_UNDEF && rt == T_NULL))) {
      equal = true;
    } else if (loose && (lt == T_NUM || lt == T_STR || lt == T_BOOL) &&
               (rt == T_NUM || rt == T_STR || rt == T_BOOL)) {
      /* ToNumber is defined below with the global numeric conversion helpers. */
      double a = dm_to_number(js, l), b = dm_to_number(js, r);
      equal = a == b;
    }
    return mkval(T_BOOL, (op == TOK_EQ || op == TOK_EQ2) ? equal : !equal);
  }
  if (vtype(l) == T_STR && vtype(r) == T_STR &&
      (op == TOK_PLUS || op == TOK_LT || op == TOK_LE || op == TOK_GT || op == TOK_GE))
    return do_string_op(js, op, l, r);
  if (is_unary(op) && op != TOK_NOT && op != TOK_TYPEOF && vtype(r) != T_NUM) {
    if (vtype(r) != T_STR && vtype(r) != T_BOOL && vtype(r) != T_NULL && vtype(r) != T_UNDEF)
      return js_mkerr(js, "type mismatch");
    r = tov(dm_to_number(js, r));
  }
  if (!is_unary(op) && op != TOK_DOT &&
      (vtype(l) != T_NUM || vtype(r) != T_NUM)) {
    uint8_t lt = vtype(l), rt = vtype(r);
    bool lprim = lt == T_NUM || lt == T_STR || lt == T_BOOL || lt == T_NULL || lt == T_UNDEF;
    bool rprim = rt == T_NUM || rt == T_STR || rt == T_BOOL || rt == T_NULL || rt == T_UNDEF;
    if (!lprim || !rprim) return js_mkerr(js, "type mismatch");
    l = tov(dm_to_number(js, l));
    r = tov(dm_to_number(js, r));
  }
  double a = tod(l), b = tod(r);
  switch (op) {
    case TOK_EXP:     return tov(pow(a, b));
    case TOK_DIV:     return tov(a / b);
    case TOK_REM:     return tov(fmod(a, b));
    case TOK_MUL:     return tov(a * b);
    case TOK_PLUS:    return tov(a + b);
    case TOK_MINUS:   return tov(a - b);
    case TOK_XOR:     return tov(js_to_int32_number(js_to_uint32(a) ^ js_to_uint32(b)));
    case TOK_AND:     return tov(js_to_int32_number(js_to_uint32(a) & js_to_uint32(b)));
    case TOK_OR:      return tov(js_to_int32_number(js_to_uint32(a) | js_to_uint32(b)));
    case TOK_UMINUS:  return tov(-b);
    case TOK_UPLUS:   return r;
    case TOK_TILDA:   return tov(js_to_int32_number(~js_to_uint32(b)));
    case TOK_NOT:     return mkval(T_BOOL, b == 0);
    case TOK_SHL: { uint32_t n = js_to_uint32(b) & 31U; return tov(js_to_int32_number(js_to_uint32(a) << n)); }
    case TOK_SHR: { uint32_t n = js_to_uint32(b) & 31U, x = js_to_uint32(a), y = x >> n; if (n && (x & 0x80000000U)) y |= (~0U << (32U - n)); return tov(js_to_int32_number(y)); }
    case TOK_ZSHR: { uint32_t n = js_to_uint32(b) & 31U; return tov((double) (js_to_uint32(a) >> n)); }
    case TOK_DOT:     return do_dot_op(js, l, r);
    case TOK_EQ:      return mkval(T_BOOL, a == b);
    case TOK_NE:      return mkval(T_BOOL, a != b);
    case TOK_LT:      return mkval(T_BOOL, a < b);
    case TOK_LE:      return mkval(T_BOOL, a <= b);
    case TOK_GT:      return mkval(T_BOOL, a > b);
    case TOK_GE:      return mkval(T_BOOL, a >= b);
    default:          return js_mkerr(js, "unknown op %d", (int) op);  // LCOV_EXCL_LINE
  }
}  // clang-format on

static jsval_t js_str_literal(struct js *js) {
  uint8_t *in = (uint8_t *) &js->code[js->toff];
  uint8_t *out = &js->mem[js->brk + sizeof(jsoff_t)];
  size_t n1 = 0, n2 = 0;
  // printf("STR %u %lu %lu\n", js->brk, js->tlen, js->clen);
  if (js->brk + sizeof(jsoff_t) + js->tlen > js->size)
    return js_mkerr(js, "oom");
  while (n2++ + 2 < js->tlen) {
    if (in[n2] == '\\') {
      if (in[n2 + 1] == in[0]) {
        out[n1++] = in[0];
      } else if (in[n2 + 1] == 'n') {
        out[n1++] = '\n';
      } else if (in[n2 + 1] == 't') {
        out[n1++] = '\t';
      } else if (in[n2 + 1] == 'r') {
        out[n1++] = '\r';
      } else if (in[n2 + 1] == 'b') {
        out[n1++] = '\b';
      } else if (in[n2 + 1] == 'f') {
        out[n1++] = '\f';
      } else if (in[n2 + 1] == 'v') {
        out[n1++] = '\v';
      } else if (in[n2 + 1] == '\\') {
        out[n1++] = '\\';
      } else if (in[n2 + 1] == 'x' && is_xdigit(in[n2 + 2]) &&
                 is_xdigit(in[n2 + 3])) {
        out[n1++] = (uint8_t) ((unhex(in[n2 + 2]) << 4U) | unhex(in[n2 + 3]));
        n2 += 2;
      } else if (in[n2 + 1] == 'u' && is_xdigit(in[n2 + 2]) &&
                 is_xdigit(in[n2 + 3]) && is_xdigit(in[n2 + 4]) &&
                 is_xdigit(in[n2 + 5])) {
        unsigned cp = ((unsigned) unhex(in[n2 + 2]) << 12U) |
                      ((unsigned) unhex(in[n2 + 3]) << 8U) |
                      ((unsigned) unhex(in[n2 + 4]) << 4U) |
                      (unsigned) unhex(in[n2 + 5]);
        if (cp == 0 || (cp >= 0xd800U && cp <= 0xdfffU))
          return js_mkerr(js, "unsupported Unicode escape");
        if (cp < 0x80U) out[n1++] = (uint8_t) cp;
        else if (cp < 0x800U) { out[n1++] = (uint8_t) (0xc0U | (cp >> 6U)); out[n1++] = (uint8_t) (0x80U | (cp & 0x3fU)); }
        else { out[n1++] = (uint8_t) (0xe0U | (cp >> 12U)); out[n1++] = (uint8_t) (0x80U | ((cp >> 6U) & 0x3fU)); out[n1++] = (uint8_t) (0x80U | (cp & 0x3fU)); }
        n2 += 4;
      } else {
        return js_mkerr(js, "bad str literal");
      }
      n2++;
    } else {
      out[n1++] = ((uint8_t *) js->code)[js->toff + n2];
    }
  }
  return js_mkstr(js, NULL, n1);
}

static jsval_t dm_array_push(struct js *, jsval_t *, int);
static jsval_t dm_array_pop(struct js *, jsval_t *, int);
static jsval_t dm_array_join(struct js *, jsval_t *, int);
static jsval_t dm_array_includes(struct js *, jsval_t *, int);
static jsval_t dm_array_indexof(struct js *, jsval_t *, int);
static jsval_t dm_array_map(struct js *, jsval_t *, int);
static jsval_t dm_array_foreach(struct js *, jsval_t *, int);
static jsval_t dm_array_filter(struct js *, jsval_t *, int);
static jsval_t dm_array_reverse(struct js *, jsval_t *, int);
static jsval_t dm_array_shift(struct js *, jsval_t *, int);
static jsval_t dm_array_unshift(struct js *, jsval_t *, int);
static jsval_t dm_array_of(struct js *, jsval_t *, int);
static jsval_t dm_array_isarray(struct js *, jsval_t *, int);
static jsval_t dm_array_from(struct js *, jsval_t *, int);
static jsval_t dm_array_slice(struct js *, jsval_t *, int);
static jsval_t dm_array_concat(struct js *, jsval_t *, int);
static jsval_t dm_array_at(struct js *, jsval_t *, int);
static jsval_t dm_array_fill(struct js *, jsval_t *, int);
static jsval_t dm_array_find(struct js *, jsval_t *, int);
static jsval_t dm_array_findindex(struct js *, jsval_t *, int);
static jsval_t dm_array_some(struct js *, jsval_t *, int);
static jsval_t dm_array_every(struct js *, jsval_t *, int);
static jsval_t dm_array_reduce(struct js *, jsval_t *, int);
static jsval_t dm_array_reduceright(struct js *, jsval_t *, int);
static jsval_t dm_array_sort(struct js *, jsval_t *, int);
static jsval_t dm_array_flat(struct js *, jsval_t *, int);
static jsval_t dm_array_flatmap(struct js *, jsval_t *, int);
static jsval_t dm_array_findlast(struct js *, jsval_t *, int);
static jsval_t dm_array_findlastindex(struct js *, jsval_t *, int);
static jsval_t dm_array_toreversed(struct js *, jsval_t *, int);
static jsval_t dm_array_tosorted(struct js *, jsval_t *, int);
static jsval_t dm_array_tospliced(struct js *, jsval_t *, int);
static jsval_t dm_array_with(struct js *, jsval_t *, int);
static jsval_t dm_array_lastindexof(struct js *, jsval_t *, int);
static jsval_t dm_array_copywithin(struct js *, jsval_t *, int);
static jsval_t dm_array_splice(struct js *, jsval_t *, int);
/* Arrays use the existing moving-heap object representation, with numeric
 * properties and an explicit length. This keeps arrays visible to the same GC. */
static int dm_array(struct js *js, jsval_t v) {
  return vtype(v) == T_OBJ && vtype(prop_value(js, v, "__dm_array")) == T_BOOL &&
         vdata(prop_value(js, v, "__dm_array")) != 0;
}
static unsigned long dm_array_length(struct js *js, jsval_t a) {
  jsval_t n = prop_value(js, a, "length");
  double d = vtype(n) == T_NUM ? tod(n) : 0;
  if (d < 0 || d > 1000000 || d != (double)(unsigned long)d) return 0;
  return (unsigned long)d;
}
/* Find which object owns a property cell. Property cells are linked from
 * object headers in the engine arena; this avoids adding POSIX/runtime state
 * or changing the compact value representation. */
static void dm_array_assignment_sync(struct js *js, jsval_t lhs, jsval_t val) {
  if (vtype(lhs) != T_PROP) return;
  jsoff_t target = (jsoff_t)(vdata(lhs) & ~3U);
  jsoff_t keyoff = loadoff(js, target + sizeof(jsoff_t));
  size_t keylen = offtolen(loadoff(js, keyoff));
  const char *key = (const char *)&js->mem[keyoff + sizeof(jsoff_t)];
  for (unsigned aidx = 0; aidx < js->dm_array_count; aidx++) {
    jsval_t owner = mkval(T_OBJ, js->dm_arrays[aidx]);
    if (lkp(js, owner, key, keylen) != target) continue;
    if (keylen == 6 && memcmp(key, "length", 6) == 0 && vtype(val) == T_NUM) {
      double d = tod(val);
      if (d < 0 || d > 1000000 || d != (double)(unsigned long)d) return;
      unsigned long newlen = (unsigned long)d;
      unsigned long oldlen = dm_array_length(js, owner);
      /* Existing slots beyond the new length become holes. */
      for (unsigned long i = newlen; i < oldlen; i++) {
        char k[32]; (void)snprintf(k, sizeof(k), "%lu", i);
        jsoff_t po = lkp(js, owner, k, strlen(k));
        if (po) saveval(js, po + sizeof(jsoff_t) * 2, mkval(T_HOLE, 0));
      }
    } else if (keylen > 0 && keylen < 16) {
      unsigned long idx = 0; int numeric = 1;
      for (size_t q = 0; q < keylen; q++) {
        if (key[q] < '0' || key[q] > '9' || (q == 0 && keylen > 1 && key[q] == '0')) { numeric = 0; break; }
        idx = idx * 10UL + (unsigned long)(key[q] - '0');
        if (idx > 1000000UL) { numeric = 0; break; }
      }
      if (numeric && idx >= dm_array_length(js, owner)) prop_set(js, owner, "length", tov((double)idx + 1));
    }
    return;
  }
}
static jsval_t dm_array_new(struct js *js) {
  jsval_t a = mkobj(js, 0);
  if (is_err(a)) return a;
  if (js->dm_array_count < sizeof(js->dm_arrays) / sizeof(js->dm_arrays[0]))
    js->dm_arrays[js->dm_array_count++] = (jsoff_t)vdata(a);
  prop_set(js, a, "__dm_array", js_mktrue());
  prop_set(js, a, "length", tov(0));
  prop_set(js, a, "push", js_mkfun(dm_array_push));
  prop_set(js, a, "pop", js_mkfun(dm_array_pop));
  prop_set(js, a, "join", js_mkfun(dm_array_join));
  prop_set(js, a, "includes", js_mkfun(dm_array_includes));
  prop_set(js, a, "indexOf", js_mkfun(dm_array_indexof));
  prop_set(js, a, "map", js_mkfun(dm_array_map));
  prop_set(js, a, "forEach", js_mkfun(dm_array_foreach));
  prop_set(js, a, "filter", js_mkfun(dm_array_filter));
  prop_set(js, a, "reverse", js_mkfun(dm_array_reverse));
  prop_set(js, a, "shift", js_mkfun(dm_array_shift));
  prop_set(js, a, "unshift", js_mkfun(dm_array_unshift));
  prop_set(js, a, "slice", js_mkfun(dm_array_slice));
  prop_set(js, a, "concat", js_mkfun(dm_array_concat));
  prop_set(js, a, "at", js_mkfun(dm_array_at));
  prop_set(js, a, "fill", js_mkfun(dm_array_fill));
  prop_set(js, a, "lastIndexOf", js_mkfun(dm_array_lastindexof));
  prop_set(js, a, "copyWithin", js_mkfun(dm_array_copywithin));
  prop_set(js, a, "splice", js_mkfun(dm_array_splice));
  prop_set(js, a, "find", js_mkfun(dm_array_find));
  prop_set(js, a, "findIndex", js_mkfun(dm_array_findindex));
  prop_set(js, a, "some", js_mkfun(dm_array_some));
  prop_set(js, a, "every", js_mkfun(dm_array_every));
  prop_set(js, a, "reduce", js_mkfun(dm_array_reduce));
  prop_set(js, a, "reduceRight", js_mkfun(dm_array_reduceright));
  prop_set(js, a, "sort", js_mkfun(dm_array_sort));
  prop_set(js, a, "flat", js_mkfun(dm_array_flat));
  prop_set(js, a, "flatMap", js_mkfun(dm_array_flatmap));
  prop_set(js, a, "findLast", js_mkfun(dm_array_findlast));
  prop_set(js, a, "findLastIndex", js_mkfun(dm_array_findlastindex));
  prop_set(js, a, "toReversed", js_mkfun(dm_array_toreversed));
  prop_set(js, a, "toSorted", js_mkfun(dm_array_tosorted));
  prop_set(js, a, "toSpliced", js_mkfun(dm_array_tospliced));
  prop_set(js, a, "with", js_mkfun(dm_array_with));
  return a;
}
static void dm_array_store(struct js *js, jsval_t a, unsigned long i, jsval_t v) {
  char key[32];
  (void) snprintf(key, sizeof(key), "%lu", i);
  prop_set(js, a, key, v);
  if (i >= dm_array_length(js, a)) prop_set(js, a, "length", tov((double)i + 1));
}
static jsval_t dm_array_push(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.push called on non-array");
  unsigned long n = dm_array_length(js, a);
  for (int i = 0; i < nargs; i++) dm_array_store(js, a, n++, args[i]);
  return tov((double)n);
}
static jsval_t dm_array_pop(struct js *js, jsval_t *args, int nargs) {
  (void) args; (void) nargs;
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.pop called on non-array");
  unsigned long n = dm_array_length(js, a);
  if (!n) return js_mkundef();
  char key[32]; (void) snprintf(key, sizeof(key), "%lu", n - 1);
  jsval_t v = prop_value(js, a, key);
  /* Undefined shadows the old slot; reducing length makes it inaccessible. */
  prop_set(js, a, key, mkval(T_HOLE, 0));
  prop_set(js, a, "length", tov((double)n - 1));
  return v;
}
static jsval_t dm_array_join(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.join called on non-array");
  const char *sep = ",";
  if (nargs > 0) { size_t sn = 0; const char *sp = js_getstr(js, args[0], &sn); if (sp) { static char sep_storage[256]; size_t take = sn < sizeof(sep_storage)-1 ? sn : sizeof(sep_storage)-1; memcpy(sep_storage, sp, take); sep_storage[take] = '\0'; sep = sep_storage; } else sep = js_str(js, args[0]); }
  char sepbuf[256]; (void) snprintf(sepbuf, sizeof(sepbuf), "%s", sep ? sep : ",");
  size_t cap = 256, used = 0; char *buf = (char *) malloc(cap);
  if (!buf) return js_mkerr(js, "out of memory in Array.join");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    jsval_t v = prop_value(js, a, key);
    const char *part = "";
    if (vtype(v) == T_STR) { size_t ignored = 0; const char *sp = js_getstr(js, v, &ignored); part = sp ? sp : ""; }
    else if (vtype(v) != T_UNDEF && vtype(v) != T_NULL) part = js_str(js, v);
    size_t pn = strlen(part), sn = i ? strlen(sepbuf) : 0;
    if (used + sn + pn + 1 > cap) {
      size_t nc = cap;
      while (nc < used + sn + pn + 1) nc *= 2;
      char *nb = (char *) realloc(buf, nc);
      if (!nb) { free(buf); return js_mkerr(js, "out of memory in Array.join"); }
      buf = nb; cap = nc;
    }
    if (sn) { memcpy(buf + used, sepbuf, sn); used += sn; }
    if (pn) { memcpy(buf + used, part, pn); used += pn; }
  }
  jsval_t result = js_mkstr(js, buf, used); free(buf); return result;
}
static int dm_array_same(jsval_t a, jsval_t b) {
  if (vtype(a) != vtype(b)) return 0;
  if (vtype(a) == T_STR) return a == b; /* strings compare by value below in helper */
  return a == b;
}
static int dm_array_equal(struct js *js, jsval_t a, jsval_t b) {
  if (vtype(a) != vtype(b)) return 0;
  if (vtype(a) == T_NUM) {
    double x = tod(a), y = tod(b);
    return x == y || (isnan(x) && isnan(y)); /* SameValueZero */
  }
  if (vtype(a) == T_STR) {
    size_t an = 0, bn = 0; const char *as = js_getstr(js, a, &an), *bs = js_getstr(js, b, &bn);
    return as && bs && an == bn && memcmp(as, bs, an) == 0;
  }
  return dm_array_same(a, b);
}
static int dm_array_strict_equal(struct js *js, jsval_t a, jsval_t b) {
  if (vtype(a) != vtype(b)) return 0;
  if (vtype(a) == T_NUM) return tod(a) == tod(b); /* NaN is never strictly equal. */
  return dm_array_equal(js, a, b);
}
static jsval_t dm_array_includes(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.includes called on non-array");
  jsval_t search = nargs ? args[0] : js_mkundef();
  unsigned long n = dm_array_length(js, a);
  long start = 0;
  if (nargs > 1 && vtype(args[1]) != T_UNDEF) {
    double d = dm_to_number(js, args[1]);
    if (isnan(d)) d = 0.0;
    if (d == INFINITY) return js_mkfalse();
    if (d == -INFINITY) d = 0.0;
    d = d < 0.0 ? ceil(d) : floor(d);
    if (d < 0.0) d += (double)n;
    if (d < 0.0) d = 0.0;
    if (d >= (double)n) return js_mkfalse();
    start = (long)d;
  }
  for (unsigned long i = (unsigned long)start; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (dm_array_equal(js, prop_value(js, a, key), search)) return js_mktrue();
  }
  return js_mkfalse();
}
static jsval_t dm_array_indexof(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.indexOf called on non-array");
  jsval_t search = nargs ? args[0] : js_mkundef();
  unsigned long n = dm_array_length(js, a);
  long start = 0;
  if (nargs > 1 && vtype(args[1]) != T_UNDEF) {
    double d = dm_to_number(js, args[1]);
    if (isnan(d)) d = 0.0;
    if (d == INFINITY) return tov(-1);
    if (d == -INFINITY) d = 0.0;
    d = d < 0.0 ? ceil(d) : floor(d);
    if (d < 0.0) d += (double)n;
    if (d < 0.0) d = 0.0;
    if (d >= (double)n) return tov(-1);
    start = (long)d;
  }
  for (unsigned long i = (unsigned long)start; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (lkp(js, a, key, strlen(key)) != 0 && dm_array_strict_equal(js, prop_value(js, a, key), search)) return tov((double)i);
  }
  return tov(-1);
}
static jsval_t dm_array_callback(struct js *js, jsval_t callback, jsval_t value,
                                 unsigned long index, jsval_t array) {
  const char *saved_code = js->code;
  jsoff_t saved_clen = js->clen, saved_pos = js->pos, saved_toff = js->toff,
          saved_tlen = js->tlen, saved_nogc = js->nogc;
  uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags;
  jsval_t saved_this = js->this_val, result;
  static const char call_args[] = "__dm_cb_value, __dm_cb_index, __dm_cb_array";
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_value", value);
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_index", tov((double)index));
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_array", array);
  js->code = call_args; js->clen = (jsoff_t)(sizeof(call_args) - 1);
  js->pos = 0; js->consumed = 1; js->flags &= (uint8_t)~F_NOEXEC;
  result = do_op(js, TOK_CALL, callback, mkcoderef(0, (jsoff_t)(sizeof(call_args) - 1)));
  js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos;
  js->toff = saved_toff; js->tlen = saved_tlen; js->tok = saved_tok;
  js->consumed = saved_consumed; js->flags = saved_flags; js->nogc = saved_nogc;
  js->this_val = saved_this;
  return result;
}
static jsval_t dm_array_callback_reduce(struct js *js, jsval_t callback, jsval_t accumulator, jsval_t value, unsigned long index, jsval_t array) {
  const char *saved_code = js->code;
  jsoff_t saved_clen = js->clen, saved_pos = js->pos, saved_toff = js->toff, saved_tlen = js->tlen, saved_nogc = js->nogc;
  uint8_t saved_tok = js->tok, saved_consumed = js->consumed, saved_flags = js->flags;
  jsval_t saved_this = js->this_val, result;
  static const char call_args[] = "__dm_cb_acc, __dm_cb_value, __dm_cb_index, __dm_cb_array";
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_acc", accumulator);
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_value", value);
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_index", tov((double) index));
  prop_set(js, mkval(T_OBJ, 0), "__dm_cb_array", array);
  js->code = call_args; js->clen = (jsoff_t) (sizeof(call_args) - 1);
  js->pos = 0; js->consumed = 1; js->flags &= (uint8_t) ~F_NOEXEC;
  result = do_op(js, TOK_CALL, callback, mkcoderef(0, (jsoff_t) (sizeof(call_args) - 1)));
  js->code = saved_code; js->clen = saved_clen; js->pos = saved_pos; js->toff = saved_toff; js->tlen = saved_tlen;
  js->tok = saved_tok; js->consumed = saved_consumed; js->flags = saved_flags; js->nogc = saved_nogc; js->this_val = saved_this;
  return result;
}
static jsval_t dm_array_map(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.map requires an array and callback");
  jsval_t out = dm_array_new(js); unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (lkp(js, a, key, strlen(key)) == 0) { prop_set(js, out, "length", tov((double)n)); continue; }
    jsval_t v = dm_array_callback(js, args[0], prop_value(js, a, key), i, a);
    if (is_err(v)) return v;
    dm_array_store(js, out, i, resolveprop(js, v));
  }
  return out;
}static jsval_t dm_array_foreach(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.forEach requires an array and callback");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (lkp(js, a, key, strlen(key)) == 0) continue;
    jsval_t v = dm_array_callback(js, args[0], prop_value(js, a, key), i, a);
    if (is_err(v)) return v;
  }
  return js_mkundef();
}
static jsval_t dm_array_filter(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.filter requires an array and callback");
  jsval_t out = dm_array_new(js); unsigned long n = dm_array_length(js, a), used = 0;
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (lkp(js, a, key, strlen(key)) == 0) continue;
    jsval_t item = prop_value(js, a, key);
    jsval_t v = dm_array_callback(js, args[0], item, i, a);
    if (is_err(v)) return v;
    if (js_truthy(js, resolveprop(js, v))) dm_array_store(js, out, used++, item);
  }
  return out;
}
static jsval_t dm_array_reverse(struct js *js, jsval_t *args, int nargs) {
  (void)args; (void)nargs; jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.reverse called on non-array");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n / 2; i++) {
    char x[32], y[32]; (void)snprintf(x, sizeof(x), "%lu", i); (void)snprintf(y, sizeof(y), "%lu", n - 1 - i);
    int xhas = lkp(js, a, x, strlen(x)) != 0, yhas = lkp(js, a, y, strlen(y)) != 0;
    jsval_t xv = prop_value(js, a, x), yv = prop_value(js, a, y);
    prop_set(js, a, x, yhas ? yv : mkval(T_HOLE, 0));
    prop_set(js, a, y, xhas ? xv : mkval(T_HOLE, 0));
  }
  return a;
}
static jsval_t dm_array_shift(struct js *js, jsval_t *args, int nargs) {
  (void)args; (void)nargs; jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.shift called on non-array");
  unsigned long n = dm_array_length(js, a); if (!n) return js_mkundef();
  jsval_t first = prop_value(js, a, "0");
  for (unsigned long i = 1; i < n; i++) { char x[32], y[32]; (void)snprintf(x,sizeof(x),"%lu",i-1); (void)snprintf(y,sizeof(y),"%lu",i); int has = lkp(js,a,y,strlen(y)) != 0; prop_set(js,a,x,has ? prop_value(js,a,y) : mkval(T_HOLE,0)); }
  char last[32]; (void)snprintf(last,sizeof(last),"%lu",n-1); prop_set(js,a,last,mkval(T_HOLE,0)); prop_set(js,a,"length",tov((double)n-1)); return first;
}
static jsval_t dm_array_unshift(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val; if (!dm_array(js,a)) return js_mkerr(js,"Array.unshift called on non-array");
  unsigned long n = dm_array_length(js,a); if (n + (unsigned long)nargs > 1000000) return js_mkerr(js,"array too large");
  for (unsigned long i=n; i>0; i--) { char x[32],y[32]; (void)snprintf(x,sizeof(x),"%lu",i-1); (void)snprintf(y,sizeof(y),"%lu",i+(unsigned long)nargs-1); int has=lkp(js,a,x,strlen(x))!=0; prop_set(js,a,y,has?prop_value(js,a,x):mkval(T_HOLE,0)); }
  for (int i=0;i<nargs;i++) { char key[32]; (void)snprintf(key,sizeof(key),"%d",i); prop_set(js,a,key,args[i]); }
  prop_set(js,a,"length",tov((double)n+nargs)); return tov((double)n+nargs);
}
static long dm_array_bound(struct js *js, jsval_t v, unsigned long n, long fallback) {
  (void) fallback;
  double d = dm_to_number(js, v);
  if (isnan(d) || d == 0.0) return 0;
  if (d == INFINITY) return (long)n;
  if (d == -INFINITY) return 0;
  d = d < 0.0 ? ceil(d) : floor(d);
  if (d < -(double)n) return 0;
  if (d < 0.0) d += (double)n;
  if (d < 0.0) return 0;
  if (d > (double)n) return (long)n;
  return (long)d;
}
static jsval_t dm_array_slice(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val; if (!dm_array(js,a)) return js_mkerr(js,"Array.slice called on non-array");
  unsigned long n=dm_array_length(js,a); long start=nargs?dm_array_bound(js,args[0],n,0):0;
  long end=nargs>1?dm_array_bound(js,args[1],n,(long)n):(long)n; if(end<start) end=start;  jsval_t out=dm_array_new(js); unsigned long j=0;
  for(long i=start;i<end;i++){char k[32],d[32];(void)snprintf(k,sizeof(k),"%ld",i);(void)snprintf(d,sizeof(d),"%lu",j);if(lkp(js,a,k,strlen(k)))dm_array_store(js,out,j,prop_value(js,a,k));else{prop_set(js,out,d,mkval(T_HOLE,0));prop_set(js,out,"length",tov((double)j+1));}j++;}
  return out;
}
static jsval_t dm_array_concat(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val; if(!dm_array(js,a)) return js_mkerr(js,"Array.concat called on non-array");
  jsval_t out=dm_array_new(js); unsigned long used=0;
  for(int pass=-1;pass<nargs;pass++){
    jsval_t src=pass<0?a:args[pass];
    if(dm_array(js,src)){unsigned long n=dm_array_length(js,src);for(unsigned long i=0;i<n;i++){char k[32],d[32];(void)snprintf(k,sizeof(k),"%lu",i);(void)snprintf(d,sizeof(d),"%lu",used);if(lkp(js,src,k,strlen(k)))dm_array_store(js,out,used,prop_value(js,src,k));else{prop_set(js,out,d,mkval(T_HOLE,0));prop_set(js,out,"length",tov((double)used+1));}used++;}}
    else if(pass>=0) dm_array_store(js,out,used++,src);
  }
  return out;
}
static jsval_t dm_array_at(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.at called on non-array");
  if (!nargs) return js_mkundef();
  double d = dm_to_number(js, args[0]);
  if (isnan(d)) d = 0.0;
  if (!isfinite(d)) return js_mkundef();
  d = d < 0.0 ? ceil(d) : floor(d);
  if (d < (double)LONG_MIN || d > (double)LONG_MAX) return js_mkundef();
  long i = (long)d;
  unsigned long n = dm_array_length(js, a);
  if (i < 0) i += (long)n;
  if (i < 0 || (unsigned long)i >= n) return js_mkundef();
  char key[32]; (void)snprintf(key, sizeof(key), "%ld", i);
  return prop_value(js, a, key);
}
static jsval_t dm_array_fill(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;if(!dm_array(js,a))return js_mkerr(js,"Array.fill called on non-array");if(!nargs)return a;
  unsigned long n=dm_array_length(js,a);long start=nargs>1?dm_array_bound(js,args[1],n,0):0;long end=nargs>2?dm_array_bound(js,args[2],n,(long)n):(long)n;
  for(long i=start;i<end;i++){char k[32];(void)snprintf(k,sizeof(k),"%ld",i);prop_set(js,a,k,args[0]);}return a;
}
static jsval_t dm_array_lastindexof(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.lastIndexOf called on non-array");
  jsval_t search = nargs ? args[0] : js_mkundef();
  long n = (long) dm_array_length(js, a);
  long i = n - 1;
  if (nargs > 1 && vtype(args[1]) != T_UNDEF) {
    double d = dm_to_number(js, args[1]);
    if (isnan(d)) return tov(-1);
    if (d < 0) d = ceil(d); else d = floor(d);
    if (d < (double)LONG_MIN) return tov(-1);
    if (d > (double)LONG_MAX) d = (double)LONG_MAX;
    i = (long)d;
    if (i < 0) i += n;
    if (i >= n) i = n - 1;
  }
  for (; i >= 0; i--) {
    char key[32]; (void)snprintf(key, sizeof(key), "%ld", i);
    if (lkp(js, a, key, strlen(key)) && dm_array_strict_equal(js, prop_value(js, a, key), search)) return tov((double)i);
  }
  return tov(-1);
}
static void dm_array_copy_slot(struct js *js, jsval_t a, unsigned long to, unsigned long from) {
  char dst[32], src[32];
  (void)snprintf(dst, sizeof(dst), "%lu", to);
  (void)snprintf(src, sizeof(src), "%lu", from);
  if (lkp(js, a, src, strlen(src))) prop_set(js, a, dst, prop_value(js, a, src));
  else prop_set(js, a, dst, mkval(T_HOLE, 0));
}
static jsval_t dm_array_copywithin(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.copyWithin called on non-array");
  if (nargs < 2) return a;
  unsigned long n = dm_array_length(js, a);
  long target = dm_array_bound(js, args[0], n, 0);
  long start = dm_array_bound(js, args[1], n, 0);
  long end = nargs > 2 ? dm_array_bound(js, args[2], n, (long)n) : (long)n;
  long count = end - start;
  if (count > (long)n - target) count = (long)n - target;
  if (count <= 0) return a;
  if (start < target && target < start + count) {
    for (long k = count; k > 0; k--) dm_array_copy_slot(js, a, (unsigned long)(target + k - 1), (unsigned long)(start + k - 1));
  } else {
    for (long k = 0; k < count; k++) dm_array_copy_slot(js, a, (unsigned long)(target + k), (unsigned long)(start + k));
  }
  return a;
}
static jsval_t dm_array_splice(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.splice called on non-array");
  unsigned long n = dm_array_length(js, a);
  long start = nargs > 0 ? dm_array_bound(js, args[0], n, 0) : 0;
  unsigned long remove_count = 0;
  if (nargs == 1) remove_count = n - (unsigned long)start;
  else if (nargs > 1) {
    double d = dm_to_number(js, args[1]);
    if (!isnan(d) && d > 0) {
      if (d > (double)(n - (unsigned long)start)) d = (double)(n - (unsigned long)start);
      remove_count = (unsigned long)floor(d);
    }
  }
  unsigned long insert_count = nargs > 2 ? (unsigned long)(nargs - 2) : 0;
  if (n - remove_count + insert_count > 1000000UL) return js_mkerr(js, "Array.splice result too large");
  jsval_t removed = dm_array_new(js);
  for (unsigned long i = 0; i < remove_count; i++) {
    unsigned long from = (unsigned long)start + i;
    char key[32]; (void)snprintf(key, sizeof(key), "%lu", from);
    if (lkp(js, a, key, strlen(key))) dm_array_store(js, removed, i, prop_value(js, a, key));    else { char dst[32]; (void)snprintf(dst, sizeof(dst), "%lu", i); prop_set(js, removed, dst, mkval(T_HOLE, 0)); prop_set(js, removed, "length", tov((double)i + 1)); }
  }
  if (insert_count < remove_count) {
    unsigned long delta = remove_count - insert_count;
    for (unsigned long i = (unsigned long)start; i < n - remove_count; i++)
      dm_array_copy_slot(js, a, i + insert_count, i + remove_count);
    for (unsigned long i = n - delta; i < n; i++) {
      char key[32]; (void)snprintf(key, sizeof(key), "%lu", i); prop_set(js, a, key, mkval(T_HOLE, 0));
    }
  } else if (insert_count > remove_count) {
    for (unsigned long i = n - remove_count; i > (unsigned long)start; i--)
      dm_array_copy_slot(js, a, i - 1 + insert_count, i - 1 + remove_count);
  }
  for (unsigned long i = 0; i < insert_count; i++) {
    char key[32]; (void)snprintf(key, sizeof(key), "%lu", (unsigned long)start + i);
    prop_set(js, a, key, args[i + 2]);
  }
  prop_set(js, a, "length", tov((double)(n - remove_count + insert_count)));
  return removed;
}

static jsval_t dm_array_reduce_impl(struct js *js, jsval_t *args, int nargs, int reverse_order) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.reduce requires a callback");
  unsigned long n = dm_array_length(js, a), i = 0;
  jsval_t accumulator = js_mkundef();
  int have_accumulator = nargs > 1;
  if (have_accumulator) accumulator = args[1];
  else {
    while (i < n) { unsigned long at = reverse_order ? n - 1 - i : i; char key[32]; (void) snprintf(key, sizeof(key), "%lu", at); if (lkp(js, a, key, strlen(key))) { accumulator = prop_value(js, a, key); have_accumulator = 1; i++; break; } i++; }
    if (!have_accumulator) return js_mkerr(js, "Reduce of empty array with no initial value");
  }
  for (; i < n; i++) {
    unsigned long at = reverse_order ? n - 1 - i : i; char key[32]; (void) snprintf(key, sizeof(key), "%lu", at);
    if (!lkp(js, a, key, strlen(key))) continue;
    jsval_t next_value = dm_array_callback_reduce(js, args[0], accumulator, prop_value(js, a, key), at, a);
    if (is_err(next_value)) return next_value;
    accumulator = resolveprop(js, next_value);
  }
  return accumulator;
}
static jsval_t dm_array_reduce(struct js *js, jsval_t *args, int nargs) { return dm_array_reduce_impl(js, args, nargs, 0); }
static jsval_t dm_array_reduceright(struct js *js, jsval_t *args, int nargs) { return dm_array_reduce_impl(js, args, nargs, 1); }
static void dm_array_flatten_into(struct js *js, jsval_t out, jsval_t value, unsigned depth) {
  if (depth > 0 && dm_array(js, value)) {
    unsigned long n = dm_array_length(js, value);
    for (unsigned long i=0;i<n;i++) { char key[32]; (void) snprintf(key,sizeof(key),"%lu",i); if (lkp(js,value,key,strlen(key))) dm_array_flatten_into(js,out,prop_value(js,value,key),depth-1); }
  } else { unsigned long n=dm_array_length(js,out); dm_array_store(js,out,n,value); }
}
static jsval_t dm_array_flat(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;if(!dm_array(js,a))return js_mkerr(js,"Array.flat called on non-array");double d=nargs>0&&vtype(args[0])==T_NUM?trunc(tod(args[0])):1;if(d<0)d=0;if(d>32)d=32;jsval_t out=dm_array_new(js);unsigned long n=dm_array_length(js,a);for(unsigned long i=0;i<n;i++){char key[32];(void)snprintf(key,sizeof(key),"%lu",i);if(lkp(js,a,key,strlen(key)))dm_array_flatten_into(js,out,prop_value(js,a,key),(unsigned)d);}return out;
}
static jsval_t dm_array_flatmap(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;if(!dm_array(js,a)||nargs<1||vtype(args[0])!=T_FUNC)return js_mkerr(js,"Array.flatMap requires a callback");jsval_t out=dm_array_new(js);unsigned long n=dm_array_length(js,a);for(unsigned long i=0;i<n;i++){char key[32];(void)snprintf(key,sizeof(key),"%lu",i);if(!lkp(js,a,key,strlen(key)))continue;jsval_t v=dm_array_callback(js,args[0],prop_value(js,a,key),i,a);if(is_err(v))return v;dm_array_flatten_into(js,out,resolveprop(js,v),1);}return out;
}
static jsval_t dm_array_sort(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;if(!dm_array(js,a))return js_mkerr(js,"Array.sort called on non-array");jsval_t sorted=dm_array_tosorted(js,args,nargs);if(is_err(sorted))return sorted;unsigned long n=dm_array_length(js,sorted);for(unsigned long i=0;i<n;i++){char key[32];(void)snprintf(key,sizeof(key),"%lu",i);if(lkp(js,sorted,key,strlen(key)))prop_set(js,a,key,prop_value(js,sorted,key));else prop_set(js,a,key,mkval(T_HOLE,0));}prop_set(js,a,"length",tov((double)n));return a;
}
static jsval_t dm_array_find(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.find requires a callback");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    jsval_t item = lkp(js, a, key, strlen(key)) ? prop_value(js, a, key) : js_mkundef();
    jsval_t test = dm_array_callback(js, args[0], item, i, a);
    if (is_err(test)) return test;
    if (js_truthy(js, resolveprop(js, test))) return item;
  }
  return js_mkundef();
}
static jsval_t dm_array_findindex(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.findIndex requires a callback");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    jsval_t item = lkp(js, a, key, strlen(key)) ? prop_value(js, a, key) : js_mkundef();
    jsval_t test = dm_array_callback(js, args[0], item, i, a);
    if (is_err(test)) return test;
    if (js_truthy(js, resolveprop(js, test))) return tov((double) i);
  }
  return tov(-1);
}
static jsval_t dm_array_some(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.some requires a callback");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (!lkp(js, a, key, strlen(key))) continue;
    jsval_t test = dm_array_callback(js, args[0], prop_value(js, a, key), i, a);
    if (is_err(test)) return test;
    if (js_truthy(js, resolveprop(js, test))) return js_mktrue();
  }
  return js_mkfalse();
}
static jsval_t dm_array_every(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 1 || vtype(args[0]) != T_FUNC) return js_mkerr(js, "Array.every requires a callback");
  unsigned long n = dm_array_length(js, a);
  for (unsigned long i = 0; i < n; i++) {
    char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
    if (!lkp(js, a, key, strlen(key))) continue;
    jsval_t test = dm_array_callback(js, args[0], prop_value(js, a, key), i, a);
    if (is_err(test)) return test;
    if (!js_truthy(js, resolveprop(js, test))) return js_mkfalse();
  }
  return js_mktrue();
}
static jsval_t dm_array_findlast(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;
  if(!dm_array(js,a)||nargs<1||vtype(args[0])!=T_FUNC) return js_mkerr(js,"Array.findLast requires a callback");
  unsigned long n=dm_array_length(js,a);
  for(unsigned long k=n;k>0;k--){unsigned long i=k-1;char key[32];(void)snprintf(key,sizeof(key),"%lu",i);jsval_t v=dm_array_callback(js,args[0],prop_value(js,a,key),i,a);if(is_err(v))return v;if(js_truthy(js,resolveprop(js,v)))return prop_value(js,a,key);}
  return js_mkundef();
}
static jsval_t dm_array_findlastindex(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;
  if(!dm_array(js,a)||nargs<1||vtype(args[0])!=T_FUNC) return js_mkerr(js,"Array.findLastIndex requires a callback");
  unsigned long n=dm_array_length(js,a);
  for(unsigned long k=n;k>0;k--){unsigned long i=k-1;char key[32];(void)snprintf(key,sizeof(key),"%lu",i);jsval_t v=dm_array_callback(js,args[0],prop_value(js,a,key),i,a);if(is_err(v))return v;if(js_truthy(js,resolveprop(js,v)))return tov((double)i);}
  return tov(-1);
}
static jsval_t dm_array_clone(struct js *js, jsval_t a) {
  jsval_t out=dm_array_new(js);unsigned long n=dm_array_length(js,a);
  for(unsigned long i=0;i<n;i++){char k[32];(void)snprintf(k,sizeof(k),"%lu",i);if(lkp(js,a,k,strlen(k)))dm_array_store(js,out,i,prop_value(js,a,k));else prop_set(js,out,"length",tov((double)i+1));}
  return out;
}
static jsval_t dm_array_toreversed(struct js *js, jsval_t *args, int nargs) {
  (void)args;(void)nargs;jsval_t a=js->this_val;if(!dm_array(js,a))return js_mkerr(js,"Array.toReversed called on non-array");
  unsigned long n=dm_array_length(js,a);jsval_t out=dm_array_new(js);
  for(unsigned long i=0;i<n;i++){char src[32];(void)snprintf(src,sizeof(src),"%lu",n-1-i);dm_array_store(js,out,i,prop_value(js,a,src));}return out;
}
static int dm_array_compare(struct js *js, jsval_t a, jsval_t b, jsval_t cmp, unsigned long ai, unsigned long bi, jsval_t arr) {
  if(vtype(cmp)==T_FUNC){
    jsval_t old0=prop_value(js,mkval(T_OBJ,0),"__dm_sort_a"),old1=prop_value(js,mkval(T_OBJ,0),"__dm_sort_b");
    prop_set(js,mkval(T_OBJ,0),"__dm_sort_a",a);prop_set(js,mkval(T_OBJ,0),"__dm_sort_b",b);
    jsval_t v=js_eval(js,"__dm_sort_cmp(__dm_sort_a,__dm_sort_b);",strlen("__dm_sort_cmp(__dm_sort_a,__dm_sort_b);"));
    prop_set(js,mkval(T_OBJ,0),"__dm_sort_a",old0);prop_set(js,mkval(T_OBJ,0),"__dm_sort_b",old1);
    if(is_err(v))return 0;return tod(resolveprop(js,v))<0?-1:tod(resolveprop(js,v))>0?1:0;
  }
  char x[128],y[128];size_t nx=tostr(js,a,x,sizeof(x)),ny=tostr(js,b,y,sizeof(y));(void)ai;(void)bi;(void)arr;
  int c=strcmp(x,y);if(nx==sizeof(x)||ny==sizeof(y))c=(int)nx-(int)ny;return c<0?-1:c>0?1:0;
}
static jsval_t dm_array_tosorted(struct js *js, jsval_t *args, int nargs) {
  jsval_t a=js->this_val;if(!dm_array(js,a))return js_mkerr(js,"Array.toSorted called on non-array");
  jsval_t out=dm_array_clone(js,a);unsigned long n=dm_array_length(js,out);jsval_t cmp=nargs?args[0]:js_mkundef();
  if(vtype(cmp)==T_FUNC)prop_set(js,mkval(T_OBJ,0),"__dm_sort_cmp",cmp);
  for(unsigned long i=1;i<n;i++){char ki[32],kj[32];(void)snprintf(ki,sizeof(ki),"%lu",i);jsval_t v=prop_value(js,out,ki);unsigned long j=i;while(j>0){(void)snprintf(kj,sizeof(kj),"%lu",j-1);jsval_t prev=prop_value(js,out,kj);int c=dm_array_compare(js,v,prev,cmp,i,j-1,out);if(c>=0)break;dm_array_store(js,out,j,prev);j--;}dm_array_store(js,out,j,v);}
  return out;
}
static jsval_t dm_array_tospliced(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a)) return js_mkerr(js, "Array.toSpliced called on non-array");
  unsigned long n = dm_array_length(js, a);
  long start = nargs ? dm_array_bound(js, args[0], n, 0) : 0;
  double dd = nargs > 1 ? dm_to_number(js, args[1]) : 0.0;
  unsigned long del = isnan(dd) || dd <= 0.0 ? 0UL :
      dd > (double)(n - (unsigned long)start) ? n - (unsigned long)start : (unsigned long)floor(dd);
  jsval_t out = dm_array_new(js);
  unsigned long used = 0;
  for (long i = 0; i < start; i++) {
    char k[32]; (void)snprintf(k, sizeof(k), "%ld", i);
    dm_array_store(js, out, used++, prop_value(js, a, k));
  }
  for (int i = 2; i < nargs; i++) dm_array_store(js, out, used++, args[i]);
  for (unsigned long i = (unsigned long)start + del; i < n; i++) {
    char k[32]; (void)snprintf(k, sizeof(k), "%lu", i);
    dm_array_store(js, out, used++, prop_value(js, a, k));
  }
  return out;
}

static jsval_t dm_array_with(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = js->this_val;
  if (!dm_array(js, a) || nargs < 2) return js_mkerr(js, "Array.with requires an index and value");
  long n = (long)dm_array_length(js, a);
  double d = dm_to_number(js, args[0]);
  if (isnan(d)) d = 0.0;
  if (!isfinite(d) || d < (double)LONG_MIN || d > (double)LONG_MAX)
    return js_mkerr(js, "Array.with index out of range");
  d = d < 0.0 ? ceil(d) : floor(d);
  long i = (long)d;
  if (i < 0) i += n;
  if (i < 0 || i >= n) return js_mkerr(js, "Array.with index out of range");
  jsval_t out = dm_array_clone(js, a);
  char key[32]; (void)snprintf(key, sizeof(key), "%ld", i);
  prop_set(js, out, key, args[1]);
  return out;
}

static jsval_t dm_array_of(struct js *js, jsval_t *args, int nargs) {
  jsval_t a = dm_array_new(js); for (int i=0;i<nargs;i++) dm_array_store(js,a,(unsigned long)i,args[i]); return a;
}
static jsval_t dm_array_isarray(struct js *js, jsval_t *args, int nargs) {
  return nargs > 0 && dm_array(js,args[0]) ? js_mktrue() : js_mkfalse();
}
static jsval_t dm_array_from(struct js *js, jsval_t *args, int nargs) {
  if (!nargs) return js_mkerr(js, "Array.from requires an array-like or string input");
  if (nargs > 1 && vtype(args[1]) != T_UNDEF && vtype(args[1]) != T_FUNC)
    return js_mkerr(js, "Array.from mapper must be a function");
  jsval_t src = resolveprop(js, args[0]), out = dm_array_new(js);
  unsigned long n = 0;
  int is_string = vtype(src) == T_STR;
  if (is_string) n = vstrlen(js, src);
  else if (vtype(src) == T_OBJ && dm_array(js, src)) n = dm_array_length(js, src);
  else if (vtype(src) == T_OBJ) {
    jsval_t length = prop_value(js, src, "length");
    double d = dm_to_number(js, length);
    if (!isfinite(d) || d <= 0.0) n = 0;
    else if (d > 1000000.0) return js_mkerr(js, "Array.from input too large");
    else n = (unsigned long)floor(d);
  } else return js_mkerr(js, "Array.from input must be a string or array-like object");
  for (unsigned long i = 0; i < n; i++) {
    jsval_t value;
    if (is_string) {
      size_t ignored = 0; const char *bytes = js_getstr(js, src, &ignored);
      value = js_mkstr(js, bytes + i, 1);
    } else {
      char key[32]; (void)snprintf(key, sizeof(key), "%lu", i);
      value = prop_value(js, src, key);
    }
    if (nargs > 1 && vtype(args[1]) == T_FUNC) {
      value = dm_array_callback(js, args[1], value, i, src);
      if (is_err(value)) return value;
      value = resolveprop(js, value);
    }
    dm_array_store(js, out, i, value);
  }
  return out;
}

static jsval_t js_array_literal(struct js *js) {
  uint8_t exe = !(js->flags & F_NOEXEC);
  jsval_t a = exe ? dm_array_new(js) : js_mkundef();
  if (is_err(a)) return a;
  js->consumed = 1;
  unsigned long i = 0;
  if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
  for (;;) {
    if (js->tok == TOK_COMMA) { /* sparse array hole */
      if (exe) prop_set(js, a, "length", tov((double)++i));
      js->consumed = 1;
      if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
      continue;
    }
    if (js->tok == TOK_ELLIPSIS) {
      js->consumed = 1;
      jsval_t spread = resolveprop(js, js_expr(js));
      if (is_err(spread)) return spread;
      if (exe) {
        if (!dm_array(js, spread)) return js_mkerr(js, "array spread currently requires an array");
        unsigned long sn = dm_array_length(js, spread);
        for (unsigned long si = 0; si < sn; si++, i++) {
          char sk[32]; (void)snprintf(sk, sizeof(sk), "%lu", si);
          if (lkp(js, spread, sk, strlen(sk)) != 0) dm_array_store(js, a, i, prop_value(js, spread, sk));
          else prop_set(js, a, "length", tov((double)i + 1));
        }
      }
      if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
      EXPECT(TOK_COMMA, );
      if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
      continue;
    }
    jsval_t v = js_expr(js);
    if (is_err(v)) return v;
    if (exe) dm_array_store(js, a, i, resolveprop(js, v));
    i++;
    if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
    EXPECT(TOK_COMMA, );
    if (v7_next_token(js) == TOK_RBRACKET) { js->consumed = 1; return a; }
  }
}

static jsval_t js_obj_literal(struct js *js) {
  uint8_t exe = !(js->flags & F_NOEXEC);
  // printf("OLIT1\n");
  jsval_t obj = exe ? mkobj(js, 0) : js_mkundef();
  if (is_err(obj)) return obj;
  js->consumed = 1;
  while (v7_next_token(js) != TOK_RBRACE) {
    if (js->tok == TOK_ELLIPSIS) {
      js->consumed = 1;
      jsval_t source = resolveprop(js, js_expr(js));
      if (is_err(source)) return source;
      if (exe && vtype(source) == T_OBJ) {
        jsoff_t off = loadoff(js, (jsoff_t)vdata(source));
        unsigned copied = 0;
        while (off && off < js->brk && copied++ < 4096) {
          jsoff_t next_off = loadoff(js, off) & ~3U;
          jsoff_t keyoff = loadoff(js, off + sizeof(jsoff_t));
          size_t keylen = offtolen(loadoff(js, keyoff));
          const char *keystr = (const char *)&js->mem[keyoff + sizeof(jsoff_t)];
          jsval_t value = loadval(js, off + sizeof(jsoff_t) * 2);
          if (value != mkval(T_HOLE, 0) && !(keylen >= 5 && memcmp(keystr, "__dm_", 5) == 0)) {
            jsval_t set = setprop(js, obj, js_mkstr(js, keystr, keylen), value);
            if (is_err(set)) return set;
          }
          off = next_off;
        }
      } else if (exe && vtype(source) != T_NULL && vtype(source) != T_UNDEF) {
        return js_mkerr(js, "object spread requires an object");
      }
      if (v7_next_token(js) == TOK_RBRACE) break;
      EXPECT(TOK_COMMA, );
      continue;
    }
    jsval_t key = 0;
    if (js->tok == TOK_IDENTIFIER) {
      if (exe) key = js_mkstr(js, js->code + js->toff, js->tlen);
    } else if (js->tok == TOK_STRING) {
      if (exe) key = js_str_literal(js);
    } else {
      return js_mkerr(js, "parse error");
    }
    js->consumed = 1;
    EXPECT(TOK_COLON, );
    jsval_t val = js_expr(js);
    if (exe) {
      // printf("XXXX [%s] scope: %lu\n", js_str(js, val), vdata(js->scope));
      if (is_err(val)) return val;
      if (is_err(key)) return key;
      jsval_t actual = resolveprop(js, val);
      size_t keylen = 0; const char *keystr = js_getstr(js, key, &keylen);
      if (keystr != NULL && keylen == 9 && memcmp(keystr, "__proto__", 9) == 0 && vtype(actual) == T_OBJ) {
        saveoff(js, (jsoff_t) (vdata(obj) + sizeof(jsoff_t)), (jsoff_t) vdata(actual));
      } else {
        jsval_t res = setprop(js, obj, key, actual);
        if (is_err(res)) return res;
      }
    }
    if (v7_next_token(js) == TOK_RBRACE) break;
    EXPECT(TOK_COMMA, );
  }
  EXPECT(TOK_RBRACE, );
  return obj;
}

static bool dm_function_source_is_strict(const char *source, size_t n) {
  size_t i = 0; unsigned depth = 0; char quote = 0;
  while (i < n && source[i] != '(') i++;
  if (i >= n) return false;
  for (; i < n; i++) {
    char c = source[i];
    if (quote) { if (c == '\\' && i + 1 < n) { i++; continue; } if (c == quote) quote = 0; continue; }
    if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
    if (i + 1 < n && c == '/' && source[i+1] == '/') { i += 2; while (i < n && source[i] != '\n') i++; continue; }
    if (i + 1 < n && c == '/' && source[i+1] == '*') { i += 2; while (i + 1 < n && !(source[i] == '*' && source[i+1] == '/')) i++; if (i + 1 < n) i++; continue; }
    if (c == '(') depth++;
    else if (c == ')' && depth && --depth == 0) { i++; break; }
  }
  i = (size_t)skiptonext(source, (jsoff_t)n, (jsoff_t)i);
  if (i >= n || source[i] != '{') return false;
  return dm_has_use_strict(source + i + 1, n - i - 1);
}

static jsval_t js_make_function(struct js *js, const char *source, size_t source_len) {
  jsval_t str = js_mkstr(js, source, source_len);
  if (vtype(str) != T_STR) return str;
  if (js->closure_count >= 128) return js_mkerr(js, "function/closure limit exceeded");
  {
    unsigned i = js->closure_count++;
    jsval_t props = mkobj(js, 0), proto = mkobj(js, 0);
    if (is_err(props) || is_err(proto)) return js_mkerr(js, "out of memory creating function");
    js->closure_fn[i] = (jsoff_t) vdata(str);
    js->closure_scope[i] = (jsoff_t) vdata(js->scope);
    js->function_props[i] = (jsoff_t) vdata(props);
    js->function_proto[i] = (jsoff_t) vdata(proto);
    js->function_arrow[i] = js->creating_arrow;
    js->function_strict[i] = (uint8_t)(js->strict_mode || dm_function_source_is_strict(source, source_len));
    js->function_this[i] = js->this_val;
    (void) setprop(js, props, js_mkstr(js, "prototype", 9), proto);
  }
  return mkval(T_FUNC, (unsigned long) vdata(str));
}

static jsval_t js_func_literal(struct js *js) {
  uint8_t flags = js->flags;  // Save current flags
  js->consumed = 1;
  EXPECT(TOK_LPAREN, js->flags = flags);
  jsoff_t pos = js->pos - 1;
  while (v7_next_token(js) != TOK_RPAREN && v7_next_token(js) != TOK_EOF) {
    if (v7_next_token(js) == TOK_ELLIPSIS) js->consumed = 1;
    uint8_t param_tok = v7_next_token(js);
    if (param_tok == TOK_LBRACE || param_tok == TOK_LBRACKET) {
      uint8_t open_tok = param_tok, close_tok = param_tok == TOK_LBRACE ? TOK_RBRACE : TOK_RBRACKET;
      unsigned depth = 1;
      js->consumed = 1;
      while (depth) {
        uint8_t part = v7_next_token(js);
        if (part == TOK_EOF || part == TOK_ERR) { js->flags = flags; return js_mkerr(js, "unterminated destructured parameter"); }
        if (part == open_tok) depth++;
        else if (part == close_tok) depth--;
        js->consumed = 1;
      }
    } else {
      EXPECT(TOK_IDENTIFIER, js->flags = flags);
      js->consumed = 1;
    }
    if (v7_next_token(js) == TOK_ASSIGN) {
      js->consumed = 1; uint8_t saved = js->flags; js->flags |= F_NOEXEC; (void)js_expr(js); js->flags = saved;
    }
    if (v7_next_token(js) == TOK_RPAREN) break;
    EXPECT(TOK_COMMA, js->flags = flags); js->consumed = 1;
  }
  EXPECT(TOK_RPAREN, js->flags = flags);
  EXPECT(TOK_LBRACE, js->flags = flags);
  js->consumed = 0;
  js->flags |= F_NOEXEC;              // Set no-execution flag to parse the
  jsval_t res = js_block(js, false);  // Skip function body - no exec
  if (is_err(res)) {                  // But fail short on parse error
    js->flags = flags;
    return res;
  }
  js->flags = flags;  // Restore flags
  jsval_t fn = js_make_function(js, &js->code[pos], js->pos - pos);
  js->consumed = 1;
  // printf("FUNC: %u [%.*s]\n", pos, js->pos - pos, &js->code[pos]);
  return fn;
}

#define RTL_BINOP(_f1, _f2, _cond)  \
  jsval_t res = _f1(js);            \
  while (!is_err(res) && (_cond)) { \
    uint8_t op = js->tok;           \
    js->consumed = 1;               \
    jsval_t rhs = _f2(js);          \
    if (is_err(rhs)) return rhs;    \
    res = do_op(js, op, res, rhs);  \
  }                                 \
  return res;

#define LTR_BINOP(_f, _cond)        \
  jsval_t res = _f(js);             \
  while (!is_err(res) && (_cond)) { \
    uint8_t op = js->tok;           \
    js->consumed = 1;               \
    jsval_t rhs = _f(js);           \
    if (is_err(rhs)) return rhs;    \
    res = do_op(js, op, res, rhs);  \
  }                                 \
  return res;

static jsval_t js_literal(struct js *js) {
  v7_next_token(js);
  setlwm(js);
  // printf("css : %u\n", js->css);
  if (js->maxcss > 0 && js->css > js->maxcss) return js_mkerr(js, "C stack");
  js->consumed = 1;
  switch (js->tok) {  // clang-format off
    case TOK_ERR:         return js_mkerr(js, "parse error");
    case TOK_NUMBER:      return js->tval;
    case TOK_STRING:      return js_str_literal(js);
    case TOK_REGEX:       return (js->flags & F_NOEXEC) ? js_mkundef() : dm_regexp_literal(js, js->code + js->toff, js->tlen);
    case TOK_LBRACE:      return js_obj_literal(js);
    case TOK_LBRACKET:    return js_array_literal(js);
    case TOK_FUNC:        return js_func_literal(js);
    case TOK_ASYNC: {
      jsval_t fn; int ai;
      uint8_t after = v7_next_token(js);
      if (after == TOK_FUNC) {
        js->consumed = 1; fn = js_func_literal(js);
      } else if (after == TOK_IDENTIFIER) {
        jsoff_t start = js->toff, len = js->tlen, arrow = js->pos;
        while (arrow < js->clen && (js->code[arrow] == ' ' || js->code[arrow] == '\t' || js->code[arrow] == '\r' || js->code[arrow] == '\n')) arrow++;
        if (arrow + 1 >= js->clen || js->code[arrow] != '=' || js->code[arrow + 1] != '>')
          return js_mkerr(js, "async must precede a function or arrow function");
        fn = js_arrow_expression(js, start, len, arrow);
      } else if (after == TOK_LPAREN) {
        jsoff_t close = 0, arrow = 0;
        if (!js_parenthesized_arrow_ahead(js, &close, &arrow))
          return js_mkerr(js, "async must precede a function or arrow function");
        fn = js_arrow_expression(js, js->toff + 1, close - (js->toff + 1), arrow);
      } else {
        return js_mkerr(js, "async must precede a function or arrow function");
      }
      ai = vtype(fn) == T_FUNC ? function_index(js, (jsoff_t) vdata(fn)) : -1;
      if (ai >= 0) js->function_async[ai] = 1;
      return fn;
    }
    case TOK_THIS:        return js->this_val;
    case TOK_NULL:        return js_mknull();
    case TOK_UNDEF:       return js_mkundef();
    case TOK_TRUE:        return js_mktrue();
    case TOK_FALSE:       return js_mkfalse();
    case TOK_IDENTIFIER: {
      jsoff_t start = js->toff, len = js->tlen, j = js->toff + js->tlen;
      while (j < js->clen && (js->code[j] == ' ' || js->code[j] == '\t' || js->code[j] == '\r' || js->code[j] == '\n')) j++;
      if (j + 1 < js->clen && js->code[j] == '=' && js->code[j + 1] == '>')
        return js_arrow_expression(js, start, len, j);
      return mkcoderef(start, len);
    }
    default:              return js_mkerr(js, "bad expr");
  }  // clang-format on
}

/* Parse expression-bodied arrow functions into the existing function format.
 * This deliberately reuses the existing parser, closures and call machinery. */
static jsval_t js_arrow_expression(struct js *js, jsoff_t param_start,
                                   jsoff_t param_len, jsoff_t arrow_pos) {
  uint8_t old_flags = js->flags;
  jsoff_t body_start = arrow_pos + 2, body_end;
  jsval_t parsed, fn;
  char *source;
  size_t source_len;
  js->pos = body_start;
  js->consumed = 1;
  js->flags |= F_NOEXEC;
  if (v7_next_token(js) == TOK_LBRACE) {
    body_start = js->toff;
    js->consumed = 1;
    parsed = js_block(js, false);
    body_end = js->pos;
    js->flags = old_flags;
    if (is_err(parsed)) return parsed;
    if (body_end < body_start) return js_mkerr(js, "invalid arrow function block");
    source_len = (size_t)param_len + (size_t)(body_end - body_start) + 24;
    source = (char *)malloc(source_len);
    if (!source) return js_mkerr(js, "out of memory parsing arrow function");
    (void)snprintf(source, source_len, "(%.*s) %.*s", (int)param_len,
                   js->code + param_start, (int)(body_end - body_start),
                   js->code + body_start);
    js->creating_arrow = 1;
    fn = js_make_function(js, source, strlen(source));
    js->creating_arrow = 0;
    free(source);
    js->consumed = 1;
    return fn;
  }
  parsed = js_expr(js);
  body_end = js->toff;
  js->flags = old_flags;
  if (is_err(parsed)) return parsed;
  if (body_end < body_start) return js_mkerr(js, "invalid arrow function body");
  source_len = (size_t) param_len + (size_t)(body_end - body_start) + 32;
  source = (char *) malloc(source_len);
  if (!source) return js_mkerr(js, "out of memory parsing arrow function");
  (void) snprintf(source, source_len, "(%.*s) { return %.*s; }",
                  (int)param_len, js->code + param_start,
                  (int)(body_end - body_start), js->code + body_start);
  js->creating_arrow = 1;
  fn = js_make_function(js, source, strlen(source));
  js->creating_arrow = 0;
  free(source);
  return fn;
}

static int js_parenthesized_arrow_ahead(struct js *js, jsoff_t *close_pos,
                                        jsoff_t *arrow_pos) {
  jsoff_t i = js->toff, depth = 0;
  char quote = 0;
  for (; i < js->clen; i++) {
    char c = js->code[i];
    if (quote) {
      if (c == '\\') { i++; continue; }
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
    if (c == '(') depth++;
    else if (c == ')' && depth && --depth == 0) {
      jsoff_t j = i + 1;
      while (j < js->clen && (js->code[j] == ' ' || js->code[j] == '\t' || js->code[j] == '\r' || js->code[j] == '\n')) j++;
      if (j + 1 < js->clen && js->code[j] == '=' && js->code[j + 1] == '>') {
        *close_pos = i; *arrow_pos = j; return 1;
      }
      return 0;
    }
  }
  return 0;
}

static jsval_t js_group(struct js *js) {
  if (v7_next_token(js) == TOK_LPAREN) {
    jsoff_t close_pos = 0, arrow_pos = 0;
    if (js_parenthesized_arrow_ahead(js, &close_pos, &arrow_pos)) {
      jsoff_t param_start = js->toff + 1;
      jsval_t fn = js_arrow_expression(js, param_start, close_pos - param_start, arrow_pos);
      return fn;
    }
    js->consumed = 1;
    jsval_t v = js_expr(js);
    if (is_err(v)) return v;
    while (v7_next_token(js) == TOK_COMMA) {
      js->consumed = 1;
      v = js_expr(js);
      if (is_err(v)) return v;
    }
    if (v7_next_token(js) != TOK_RPAREN) return js_mkerr(js, ") expected");
    js->consumed = 1;
    return v;
  } else {
    return js_literal(js);
  }
}

static jsval_t js_call_dot(struct js *js) {
  jsval_t res = js_group(js), receiver = js_mkundef();
  int chain_short = 0;
  if (is_err(res)) return res;
  if (vtype(res) == T_CODEREF) res = lookup(js, &js->code[coderefoff(res)], codereflen(res));
  while (v7_next_token(js) == TOK_LPAREN || v7_next_token(js) == TOK_DOT || v7_next_token(js) == TOK_LBRACKET || v7_next_token(js) == TOK_OPTCHAIN) {
    uint8_t op = v7_next_token(js);
    int optional = op == TOK_OPTCHAIN;
    if (optional) {
      js->consumed = 1;
      if (!chain_short) {
        jsval_t base = resolveprop(js, res);
        if (vtype(base) == T_NULL || vtype(base) == T_UNDEF) chain_short = 1;
      }
      op = v7_next_token(js);
      if (op == TOK_IDENTIFIER || (op >= TOK_BREAK && op <= TOK_OF)) op = TOK_DOT;
      else if (op != TOK_LBRACKET && op != TOK_LPAREN) return js_mkerr(js, "property, index or call expected after '?.'");
    }
    if (chain_short) {
      uint8_t old_flags = js->flags;
      js->flags |= F_NOEXEC;
      if (op == TOK_DOT) {
        if (optional) js->consumed = 1;
        else {
          js->consumed = 1;
          if (v7_next_token(js) == TOK_EOF) { js->flags = old_flags; return js_mkerr(js, "property name expected after optional chain"); }
          js->consumed = 1;
        }
      } else if (op == TOK_LBRACKET) {
        js->consumed = 1;
        (void)js_expr(js);
        if (v7_next_token(js) != TOK_RBRACKET) { js->flags = old_flags; return js_mkerr(js, "']' expected"); }
        js->consumed = 1;
      } else if (op == TOK_LPAREN) {
        (void)js_call_params(js);
      }
      js->flags = old_flags;
      res = js_mkundef();
      receiver = js_mkundef();
      continue;
    }
    if (op == TOK_DOT) {
      jsval_t key;
      receiver = resolveprop(js, res);
      if (!optional) js->consumed = 1;
      if (v7_next_token(js) == TOK_EOF) return js_mkerr(js, "property name expected after dot");
      if (js->tok != TOK_IDENTIFIER && !(js->tok >= TOK_BREAK && js->tok <= TOK_OF)) return js_mkerr(js, "property name expected after dot");
      key = mkcoderef((jsoff_t) js->toff, (jsoff_t) js->tlen);
      js->consumed = 1;
      res = do_op(js, TOK_DOT, res, key);
    } else if (op == TOK_LBRACKET) {
      receiver = resolveprop(js, res);
      if (!optional) js->consumed = 1;
      jsval_t key = js_expr(js);
      if (is_err(key)) return key;
      if (v7_next_token(js) != TOK_RBRACKET) return js_mkerr(js, "']' expected");
      js->consumed = 1;
      res = do_op(js, TOK_DOT, res, resolveprop(js, key));
    } else {
      jsval_t params = js_call_params(js);
      jsval_t old_this = js->this_val;
      if (is_err(params)) return params;
      js->this_val = receiver;
      res = do_op(js, TOK_CALL, res, params);
      js->this_val = old_this;
      receiver = js_mkundef();
    }
    if (is_err(res)) return res;
  }
  return res;
}
static jsval_t js_postfix(struct js *js) {
  jsval_t res = js_call_dot(js);
  if (is_err(res)) return res;
  v7_next_token(js);
  if (js->tok == TOK_POSTINC || js->tok == TOK_POSTDEC) {
    js->consumed = 1;
    res = do_op(js, js->tok, res, 0);
  }
  return res;
}

static jsval_t function_prototype_value(struct js *js, jsval_t fn) {
  jsval_t key = js_mkstr(js, "prototype", 9);
  return resolveprop(js, do_dot_op(js, fn, key));
}

static jsval_t js_new(struct js *js) {
  jsval_t ctor, params, proto = js_mkundef(), instance, result;
  int fi;
  js->consumed = 1;
  ctor = js_group(js);
  if (is_err(ctor)) return ctor;
  if (vtype(ctor) == T_CODEREF) ctor = lookup(js, &js->code[coderefoff(ctor)], codereflen(ctor));
  ctor = resolveprop(js, ctor);
  if (vtype(ctor) != T_FUNC) return js_mkerr(js, "new requires a function constructor");
  fi = function_index(js, (jsoff_t) vdata(ctor));
  if (fi >= 0 && js->function_arrow[fi]) return js_mkerr(js, "arrow function is not a constructor");
  if (fi >= 0) proto = function_prototype_value(js, ctor);
  if (v7_next_token(js) != TOK_LPAREN) return js_mkerr(js, "'(' expected after constructor");
  params = js_call_params(js);
  if (is_err(params)) return params;
  instance = mkobj(js, vtype(proto) == T_OBJ ? (jsoff_t) vdata(proto) : 0);
  {
    jsval_t old_this = js->this_val;
    jsval_t parent_ctor = prop_value(js, ctor, "__dm_super");
    js->this_val = instance;
    if (vtype(parent_ctor) == T_FUNC) {
      result = do_op(js, TOK_CALL, parent_ctor, params);
      if (is_err(result)) { js->this_val = old_this; return result; }
    }
    result = do_op(js, TOK_CALL, ctor, params);
    js->this_val = old_this;
  }
  if (is_err(result)) return result;
  result = vtype(result) == T_OBJ ? result : instance;
  /* Parse member and call suffixes on the constructed instance. */
  {
    jsval_t receiver = js_mkundef();
    while (v7_next_token(js) == TOK_DOT || v7_next_token(js) == TOK_LBRACKET ||
           v7_next_token(js) == TOK_LPAREN) {
      uint8_t op = v7_next_token(js);
      if (op == TOK_DOT) {
        jsval_t key;
        receiver = resolveprop(js, result);
        js->consumed = 1;
        if (v7_next_token(js) == TOK_EOF ||
            (js->tok != TOK_IDENTIFIER && !(js->tok >= TOK_BREAK && js->tok <= TOK_OF)))
          return js_mkerr(js, "property name expected after dot");
        key = mkcoderef((jsoff_t) js->toff, (jsoff_t) js->tlen);
        js->consumed = 1;
        result = do_op(js, TOK_DOT, result, key);
      } else if (op == TOK_LBRACKET) {
        receiver = resolveprop(js, result);
        js->consumed = 1;
        jsval_t key = js_expr(js);
        if (is_err(key)) return key;
        if (v7_next_token(js) != TOK_RBRACKET) return js_mkerr(js, "']' expected");
        js->consumed = 1;
        result = do_op(js, TOK_DOT, result, resolveprop(js, key));
      } else {
        jsval_t params = js_call_params(js), old_this = js->this_val;
        if (is_err(params)) return params;
        js->this_val = receiver;
        result = do_op(js, TOK_CALL, result, params);
        js->this_val = old_this;
        receiver = js_mkundef();
      }
      if (is_err(result)) return result;
    }
  }
  return result;
}

static jsval_t js_unary(struct js *js) {
  if (v7_next_token(js) == TOK_DELETE) {
    js->consumed = 1;
    jsval_t target = js_postfix(js);
    if (is_err(target)) return target;
    if (js->flags & F_NOEXEC) return js_mktrue();
    if (vtype(target) == T_PROP) {
      jsoff_t off=(jsoff_t)vdata(target); uint32_t flags=prop_flags(js,off);
      if (!(flags & V7_PROP_CONFIGURABLE)) return js->strict_mode ? js_mkerr(js,"cannot delete non-configurable property") : js_mkfalse();
      saveval(js, off + sizeof(jsoff_t) * 2, mkval(T_HOLE, 0));
      return js_mktrue();
    }
    return js_mkfalse();
  }
  if (v7_next_token(js) == TOK_VOID) {
    jsval_t v;
    js->consumed = 1;
    v = js_unary(js);
    return is_err(v) ? v : js_mkundef();
  }
  if (v7_next_token(js) == TOK_POSTINC || js->tok == TOK_POSTDEC) {
    uint8_t prefix = js->tok == TOK_POSTINC ? TOK_PREINC : TOK_PREDEC;
    js->consumed = 1;
    jsval_t target = js_unary(js);
    if (is_err(target)) return target;
    return do_op(js, prefix, target, js_mkundef());
  }
  if (v7_next_token(js) == TOK_AWAIT) {
    jsval_t v;
    js->consumed = 1; v = resolveprop(js, js_unary(js));
    if (is_err(v)) return v;
    if (!is_promise(js, v)) return v;
    if (promise_state(js, v) == 1) return promise_value(js, v);
    if (promise_state(js, v) == -1) { js->thrown = promise_value(js, v); js->has_thrown = 1; return js_mkerr(js, "await rejected promise"); }
    /* Compact interpreter fallback: pump host tasks until this Promise settles.
     * This resumes the current C interpreter stack, but is not yet a fully
     * non-blocking continuation implementation. */
    while (promise_state(js, v) == 0) {
      drain_microtasks(js);
      if (promise_state(js, v) != 0) break;
      if (!run_one_event(js, 1)) return js_mkerr(js, "await pending Promise with no runnable host task");
    }
    if (promise_state(js, v) == -1) { js->thrown = promise_value(js, v); js->has_thrown = 1; return js_mkerr(js, "await rejected promise"); }
    return promise_value(js, v);
  }
  if (v7_next_token(js) == TOK_NEW) return js_new(js);
  if (v7_next_token(js) == TOK_NOT || js->tok == TOK_TILDA || js->tok == TOK_TYPEOF ||
      js->tok == TOK_MINUS || js->tok == TOK_PLUS) {
    uint8_t t = js->tok;
    if (t == TOK_MINUS) t = TOK_UMINUS;
    if (t == TOK_PLUS) t = TOK_UPLUS;
    if (t == TOK_TYPEOF && !(js->flags & F_NOEXEC)) {
      jsoff_t start = skiptonext(js->code, js->clen, js->pos), ident_len = 0;
      if (start < js->clen && is_ident_begin((unsigned char) js->code[start]) &&
          parseident(js->code + start, js->clen - start, &ident_len) == TOK_IDENTIFIER) {
        jsoff_t after = skiptonext(js->code, js->clen, start + ident_len);
        bool has_binding = false;
        for (jsval_t scope = js->scope; vtype(scope) == T_OBJ;) {
          if (lkp(js, scope, js->code + start, ident_len) != 0) { has_binding = true; break; }
          jsval_t parent = upper(js, scope);
          if (vdata(parent) == vdata(scope) || vdata(scope) == 0) break;
          scope = parent;
        }
        if (!has_binding && (after >= js->clen ||
            (js->code[after] != '.' && js->code[after] != '[' && js->code[after] != '('))) {
          js->pos = start + ident_len; js->tok = TOK_IDENTIFIER;
          js->toff = start; js->tlen = ident_len; js->consumed = 1;
          return js_mkstr(js, "undefined", 9);
        }
      }
    }
    js->consumed = 1;
    return do_op(js, t, js_mkundef(), js_unary(js));
  } else {
    return js_postfix(js);
  }
}

static jsval_t js_power(struct js *js) {
  jsval_t left = js_unary(js);
  if (is_err(left)) return left;
  if (v7_next_token(js) == TOK_EXP) {
    js->consumed = 1;
    jsval_t right = js_power(js);  // right associative: 2 ** 3 ** 2 == 512
    if (is_err(right)) return right;
    return do_op(js, TOK_EXP, left, right);
  }
  return left;
}

static jsval_t js_mul_div_rem(struct js *js) {
  LTR_BINOP(js_power,
            (v7_next_token(js) == TOK_MUL || js->tok == TOK_DIV || js->tok == TOK_REM));
}

static jsval_t js_plus_minus(struct js *js) {
  LTR_BINOP(js_mul_div_rem, (v7_next_token(js) == TOK_PLUS || js->tok == TOK_MINUS));
}

static jsval_t js_shifts(struct js *js) {
  LTR_BINOP(js_plus_minus, (v7_next_token(js) == TOK_SHR || v7_next_token(js) == TOK_SHL ||
                            v7_next_token(js) == TOK_ZSHR));
}
static jsval_t js_comparison(struct js *js) {
  LTR_BINOP(js_shifts, (v7_next_token(js) == TOK_LT || v7_next_token(js) == TOK_LE ||
                        v7_next_token(js) == TOK_GT || v7_next_token(js) == TOK_GE || v7_next_token(js) == TOK_IN ||
                        v7_next_token(js) == TOK_INSTANCEOF));
}

static jsval_t js_equality(struct js *js) {
  LTR_BINOP(js_comparison, (v7_next_token(js) == TOK_EQ || js->tok == TOK_NE ||
                            js->tok == TOK_EQ2 || js->tok == TOK_NE2));
}

static jsval_t js_bitwise_and(struct js *js) {
  LTR_BINOP(js_equality, (v7_next_token(js) == TOK_AND));
}

static jsval_t js_bitwise_xor(struct js *js) {
  LTR_BINOP(js_bitwise_and, (v7_next_token(js) == TOK_XOR));
}

static jsval_t js_bitwise_or(struct js *js) {
  LTR_BINOP(js_bitwise_xor, (v7_next_token(js) == TOK_OR));
}

static jsval_t js_logical_and(struct js *js) {
  jsval_t res = js_bitwise_or(js);
  if (is_err(res)) return res;
  uint8_t flags = js->flags;
  while (v7_next_token(js) == TOK_LAND) {
    js->consumed = 1;
    res = resolveprop(js, res);
    if (!js_truthy(js, res)) js->flags |= F_NOEXEC;  // false && ... shortcut
    if (js->flags & F_NOEXEC) {
      js_logical_and(js);
    } else {
      res = js_logical_and(js);
    }
  }
  js->flags = flags;
  return res;
}

static jsval_t js_nullish(struct js *js) {
  jsval_t res = js_logical_and(js);
  if (is_err(res)) return res;
  uint8_t flags = js->flags;
  while (v7_next_token(js) == TOK_NULLISH) {
    js->consumed = 1;
    jsval_t left = resolveprop(js, res);
    int is_nullish = vtype(left) == T_NULL || vtype(left) == T_UNDEF;
    if (!is_nullish) js->flags |= F_NOEXEC;
    jsval_t right = js_logical_and(js);
    if (is_err(right)) { js->flags = flags; return right; }
    if (is_nullish && !(flags & F_NOEXEC)) res = right;
    js->flags = flags;
  }
  return res;
}

static jsval_t js_logical_or(struct js *js) {
  jsval_t res = js_nullish(js);
  if (is_err(res)) return res;
  uint8_t flags = js->flags;
  while (v7_next_token(js) == TOK_LOR) {
    js->consumed = 1;
    res = resolveprop(js, res);
    if (js_truthy(js, res)) js->flags |= F_NOEXEC;  // true || ... shortcut
    if (js->flags & F_NOEXEC) {
      js_logical_or(js);
    } else {
      res = js_logical_or(js);
    }
  }
  js->flags = flags;
  return res;
}

static jsval_t js_ternary(struct js *js) {
  jsval_t res = js_logical_or(js);
  if (v7_next_token(js) == TOK_Q) {
    uint8_t flags = js->flags;
    js->consumed = 1;
    if (js_truthy(js, resolveprop(js, res))) {
      res = js_ternary(js);
      js->flags |= F_NOEXEC;
      EXPECT(TOK_COLON, js->flags = flags);
      js_ternary(js);
      js->flags = flags;
    } else {
      js->flags |= F_NOEXEC;
      js_ternary(js);
      EXPECT(TOK_COLON, js->flags = flags);
      js->flags = flags;
      res = js_ternary(js);
    }
  }
  return res;
}

static jsval_t js_assignment(struct js *js) {
  jsval_t lhs = js_ternary(js);
  while (!is_err(lhs) && is_assign(v7_next_token(js))) {
    uint8_t op = js->tok;
    uint8_t flags = js->flags;
    jsval_t rhs;
    js->consumed = 1;
    if (op == TOK_NULLISH_ASSIGN) {
      jsval_t current = resolveprop(js, lhs);
      bool should_assign = !(flags & F_NOEXEC) &&
          (vtype(current) == T_NULL || vtype(current) == T_UNDEF);
      if (!should_assign) js->flags |= F_NOEXEC;
      rhs = js_assignment(js);
      js->flags = flags;
      if (is_err(rhs)) return rhs;
      if (should_assign) lhs = assign(js, lhs, rhs);
      else lhs = current;
    } else {
      rhs = js_assignment(js);
      if (is_err(rhs)) return rhs;
      lhs = do_op(js, op, lhs, rhs);
    }
  }
  return lhs;
}
static jsval_t js_expr(struct js *js) {
  return js_assignment(js);
}

static void dm_mark_const(struct js *js, const char *name, size_t len) {
  char marker[192];
  if (len > 150) return;
  (void)snprintf(marker, sizeof(marker), "__dm_const_%.*s", (int)len, name);
  (void)setprop(js, js->scope, js_mkstr(js, marker, strlen(marker)), js_mktrue());
}
static int dm_const_lvalue(struct js *js, jsval_t lhs) {
  if (vtype(lhs) != T_PROP) return 0;
  jsoff_t poff=(jsoff_t)vdata(lhs)&~3U;
  jsoff_t keyoff=loadoff(js,poff+(jsoff_t)sizeof(jsoff_t));
  if(!keyoff)return 0;
  jsoff_t enc=loadoff(js,keyoff);size_t len=offtolen(enc);if(len==0)return 0;
  const char *name=(const char*)&js->mem[keyoff+sizeof(jsoff_t)];
  jsval_t scope=js->scope;
  for(unsigned depth=0;depth<128 && vtype(scope)==T_OBJ;depth++){
    if(lkp(js,scope,name,len)==poff){
      char marker[192];if(len>150)return 0;(void)snprintf(marker,sizeof(marker),"__dm_const_%.*s",(int)len,name);
      jsoff_t m=lkp(js,scope,marker,strlen(marker));
      return m && vtype(loadval(js,m+sizeof(jsoff_t)*2))==T_BOOL && vdata(loadval(js,m+sizeof(jsoff_t)*2))!=0;
    }
    jsval_t parent=upper(js,scope);if(vdata(parent)==vdata(scope)||vdata(scope)==0)break;scope=parent;
  }
  return 0;
}

static jsval_t js_let(struct js *js) {
  uint8_t exe = !(js->flags & F_NOEXEC);
  uint8_t decl_tok = js->tok;
  int is_const = decl_tok == TOK_CONST;
  jsval_t decl_scope = decl_tok == TOK_VAR ? dm_var_scope(js) : js->scope;
  js->consumed = 1;
  /* Object destructuring declaration, including simple aliases:
   * let {name, id: userId} = source. Nested patterns are handled below only
   * for the flat forms the compact runtime can represent safely. */
  if (v7_next_token(js) == TOK_LBRACE) {
    jsoff_t keys[64], binds[64]; unsigned count = 0;
    js->consumed = 1;
    while (v7_next_token(js) != TOK_RBRACE && v7_next_token(js) != TOK_EOF) {
      if (js->tok != TOK_IDENTIFIER) return js_mkerr(js, "property name expected in object destructuring");
      if (count >= 64) return js_mkerr(js, "object destructuring limit exceeded");
      keys[count] = js->toff; binds[count] = js->toff;
      js->consumed = 1;
      if (v7_next_token(js) == TOK_COLON) {
        js->consumed = 1;
        if (v7_next_token(js) != TOK_IDENTIFIER) return js_mkerr(js, "binding identifier expected after ':'");
        binds[count] = js->toff; js->consumed = 1;
      }
      count++;
      if (v7_next_token(js) == TOK_RBRACE) break;
      if (js->tok != TOK_COMMA) return js_mkerr(js, "comma expected in object destructuring");
      js->consumed = 1;
    }
    if (js->tok != TOK_RBRACE) return js_mkerr(js, "'}' expected in object destructuring");
    js->consumed = 1;
    if (v7_next_token(js) != TOK_ASSIGN) return js_mkerr(js, "'=' expected after object destructuring");
    js->consumed = 1;
    jsval_t value = js_expr(js);
    if (is_err(value)) return value;
    value = resolveprop(js, value);
    if (exe) for (unsigned i = 0; i < count; i++) {
      size_t kn = 0, bn = 0;
      while (js->code[keys[i] + kn] && is_ident_continue(js->code[keys[i] + kn])) kn++;
      while (js->code[binds[i] + bn] && is_ident_continue(js->code[binds[i] + bn])) bn++;
      jsval_t item = vtype(value) == T_OBJ ? resolveprop(js, do_dot_op(js, value, js_mkstr(js, js->code + keys[i], kn))) : js_mkundef();
      if (lkp(js, decl_scope, js->code + binds[i], bn) > 0) return js_mkerr(js, "'%.*s' already declared", (int)bn, js->code + binds[i]);
      jsval_t set = setprop(js, decl_scope, js_mkstr(js, js->code + binds[i], bn), item);
      if (is_err(set)) return set;
      if (is_const) dm_mark_const(js, js->code + binds[i], bn);    }
    return js_mkundef();
  }
  /* Basic array destructuring declaration: let [a, b] = array. */
  if (v7_next_token(js) == TOK_LBRACKET) {
    jsoff_t names[64], lens[64]; unsigned count = 0;
    js->consumed = 1;
    while (v7_next_token(js) != TOK_RBRACKET && v7_next_token(js) != TOK_EOF) {
      if (js->tok != TOK_IDENTIFIER) return js_mkerr(js, "identifier expected in array destructuring");
      if (count >= 64) return js_mkerr(js, "array destructuring limit exceeded");
      names[count] = js->toff; lens[count] = js->tlen; count++;
      js->consumed = 1;
      if (v7_next_token(js) == TOK_RBRACKET) break;
      if (js->tok != TOK_COMMA) return js_mkerr(js, "comma expected in array destructuring");
      js->consumed = 1;
    }
    if (js->tok != TOK_RBRACKET) return js_mkerr(js, "']' expected in array destructuring");
    js->consumed = 1;
    if (v7_next_token(js) != TOK_ASSIGN) return js_mkerr(js, "'=' expected after array destructuring");
    js->consumed = 1;
    jsval_t value = js_expr(js);
    if (is_err(value)) return value;
    value = resolveprop(js, value);
    if (exe) for (unsigned i = 0; i < count; i++) {
      char key[32]; (void) snprintf(key, sizeof(key), "%u", i);
      jsval_t item = vtype(value) == T_OBJ ? prop_value(js, value, key) : js_mkundef();
      if (lkp(js, decl_scope, (const char *)&js->code[names[i]], lens[i]) > 0)
        return js_mkerr(js, "'%.*s' already declared", (int)lens[i], js->code + names[i]);
      jsval_t set = setprop(js, decl_scope, js_mkstr(js, js->code + names[i], lens[i]), item);
      if (is_err(set)) return set;
      if (is_const) dm_mark_const(js, js->code + names[i], lens[i]);
    }
    return js_mkundef();
  }
  for (;;) {
    EXPECT(TOK_IDENTIFIER, );
    js->consumed = 0;
    jsoff_t noff = js->toff, nlen = js->tlen;
    char *name = (char *) &js->code[noff];
    jsval_t v = js_mkundef();
    js->consumed = 1;
    int initialized = 0;
    if (v7_next_token(js) == TOK_ASSIGN) {
      initialized = 1; js->consumed = 1;
      v = js_expr(js);
      if (is_err(v)) return v;
    }
    if (is_const && !initialized) return js_mkerr(js, "const declaration requires an initializer");
    if (exe) {
      jsoff_t existing = lkp(js, decl_scope, name, nlen);
      if (decl_tok != TOK_VAR && existing > 0)
        return js_mkerr(js, "'%.*s' already declared", (int) nlen, name);
      /* A bare `var name;` declares/hoists a binding but must not reset an
       * earlier assignment to undefined. Only an initializer writes it. */
      if (decl_tok != TOK_VAR || initialized || existing == 0) {
        jsval_t x = setprop(js, decl_scope, js_mkstr(js, name, nlen), resolveprop(js, v));
        if (is_err(x)) return x;
      }
      if (is_const) dm_mark_const(js, name, nlen);
    }
    {
      uint8_t next = v7_next_token(js);
      if (next == TOK_SEMICOLON || next == TOK_EOF) break;
      if (dm_linebreak_before(js->code, (size_t) js->toff)) {
        js->consumed = 0;
        break;
      }
    }
    EXPECT(TOK_COMMA, );
  }
  return js_mkundef();
}

static jsval_t js_block_or_stmt(struct js *js) {
  if (v7_next_token(js) == TOK_LBRACE) return js_block(js, !(js->flags & F_NOEXEC));
  jsval_t res = resolveprop(js, js_stmt(js));
  js->consumed = 0;  //
  return res;
}

static jsval_t js_if(struct js *js) {
  js->consumed = 1;
  EXPECT(TOK_LPAREN, );
  jsval_t res = js_mkundef(), cond = resolveprop(js, js_expr(js));
  EXPECT(TOK_RPAREN, );
  bool cond_true = js_truthy(js, cond), exe = !(js->flags & F_NOEXEC);
  // printf("IF COND: %s, true? %d\n", js_str(js, cond), cond_true);
  if (!cond_true) js->flags |= F_NOEXEC;
  jsval_t blk = js_block_or_stmt(js);
  if (cond_true) res = blk;
  if (exe && !cond_true) js->flags &= (uint8_t) ~F_NOEXEC;
  if (lookahead(js) == TOK_ELSE) {
    js->consumed = 1;
    v7_next_token(js);
    js->consumed = 1;
    if (cond_true) js->flags |= F_NOEXEC;
    blk = js_block_or_stmt(js);
    if (!cond_true) res = blk;
    if (cond_true && exe) js->flags &= (uint8_t) ~F_NOEXEC;
  }
  return res;
}

static inline bool expect(struct js *js, uint8_t tok, jsval_t *res) {
  if (v7_next_token(js) != tok) {
    *res = js_mkerr(js, "parse error");
    return false;
  } else {
    js->consumed = 1;
    return true;
  }
}

static inline bool is_err2(jsval_t *v, jsval_t *res) {
  bool r = is_err(*v);
  if (r) *res = *v;
  return r;
}

static jsval_t dm_forof_values(struct js *js, jsval_t iterable);

static jsval_t js_for(struct js *js) {
  uint8_t flags = js->flags, exe = !(flags & F_NOEXEC);
  jsval_t v, res = js_mkundef();
  jsoff_t pos1 = 0, pos2 = 0, pos3 = 0, pos4 = 0;
  if (exe) mkscope(js);  // Enter new scope
  if (!expect(js, TOK_FOR, &res)) goto done;
  if (!expect(js, TOK_LPAREN, &res)) goto done;

  /* Array-based for...of plus own-key for...in. The runtime keeps its
   * existing compact object/array representation; this is not a full generic
   * iterator protocol implementation. */
  {
    jsoff_t probe_pos = js->pos, var_off = 0, body_start = 0, body_end = 0;
    uint8_t probe_consumed = js->consumed, decl_kind = TOK_ERR, iteration_kind = TOK_ERR;
    size_t var_len = 0;
    int matched = 0;
    uint8_t first = v7_next_token(js);
    if (first == TOK_LET || first == TOK_CONST || first == TOK_VAR) {
      decl_kind = first;
      js->consumed = 1;
      if (v7_next_token(js) == TOK_IDENTIFIER) {
        var_off = js->toff; var_len = js->tlen; js->consumed = 1;
        iteration_kind = v7_next_token(js);
        if (iteration_kind == TOK_OF || iteration_kind == TOK_IN) matched = 1;
      }
    }
    if (matched) {
      js->consumed = 1;
      jsval_t iterable = resolveprop(js, js_expr(js));
      if (is_err(iterable)) { res = iterable; goto done; }
      if (!expect(js, TOK_RPAREN, &res)) goto done;
      body_start = js->pos;
      js->flags |= F_NOEXEC;
      v = js_block_or_stmt(js);
      if (is_err2(&v, &res)) goto done;
      body_end = js->pos;
      if (exe) {
        jsval_t binding_scope = decl_kind == TOK_VAR ? dm_var_scope(js) : js->scope;
        jsval_t name = js_mkstr(js, js->code + var_off, var_len);
        jsval_t items;
        unsigned long n;
        if (iteration_kind == TOK_OF) {
          items = dm_forof_values(js, iterable);
          if (is_err(items)) { res = items; goto done; }
        } else {
          if (vtype(iterable) != T_OBJ && vtype(iterable) != T_FUNC) {
            res = js_mkerr(js, "right side of for...in must be an object"); goto done;
          }
          items = dm_object_keys(js, &iterable, 1);
        }
        if (is_err(items)) { res = items; goto done; }
        n = dm_array_length(js, items);
        if (decl_kind != TOK_VAR && lkp(js, binding_scope, js->code + var_off, var_len) != 0) {
          res = js_mkerr(js, "'%.*s' already declared", (int)var_len, js->code + var_off); goto done;
        }
        if (decl_kind == TOK_CONST) dm_mark_const(js, js->code + var_off, var_len);
        for (unsigned long i = 0; i < n; i++) {
          char k[32]; (void) snprintf(k, sizeof(k), "%lu", i);
          jsval_t item = prop_value(js, items, k);
          if (vtype(item) == T_HOLE) item = js_mkundef();
          if (iteration_kind == TOK_IN && dm_array(js, iterable) && vtype(item) == T_STR) {
            size_t key_len = 0; const char *key = js_getstr(js, item, &key_len);
            if (key != NULL && key_len == 6 && memcmp(key, "length", 6) == 0) continue;
          }
          (void)setprop(js, binding_scope, name, item);
          js->flags = flags | F_LOOP; js->pos = body_start; js->consumed = 1;
          v = js_block_or_stmt(js);
          if (is_err2(&v, &res)) goto done;
          if (js->flags & F_RETURN) { res = v; goto done; }
          if (js->flags & F_BREAK) break;
        }
      }
      js->pos = body_end; js->tok = TOK_SEMICOLON; js->consumed = 0;
      goto done;
    }
    js->pos = probe_pos; js->consumed = probe_consumed;
  }

  if (v7_next_token(js) == TOK_SEMICOLON) {  // initialisation
  } else if (v7_next_token(js) == TOK_LET || js->tok == TOK_VAR || js->tok == TOK_CONST) {
    v = js_let(js);
    if (is_err2(&v, &res)) goto done;
  } else {
    v = js_expr(js);
    if (is_err2(&v, &res)) goto done;  }
  if (!expect(js, TOK_SEMICOLON, &res)) goto done;
  js->flags |= F_NOEXEC;
  pos1 = js->pos;  // condition
  if (v7_next_token(js) != TOK_SEMICOLON) {
    v = js_expr(js);
    if (is_err2(&v, &res)) goto done;
  }
  if (!expect(js, TOK_SEMICOLON, &res)) goto done;
  pos2 = js->pos;  // final expr
  if (v7_next_token(js) != TOK_RPAREN) {
    v = js_expr(js);
    if (is_err2(&v, &res)) goto done;
  }
  if (!expect(js, TOK_RPAREN, &res)) goto done;
  pos3 = js->pos;  // body
  v = js_block_or_stmt(js);
  if (is_err2(&v, &res)) goto done;
  pos4 = js->pos;  // end of body
  while (!(flags & F_NOEXEC)) {
    js->flags = flags, js->pos = pos1, js->consumed = 1;
    if (v7_next_token(js) != TOK_SEMICOLON) {     // Is condition specified?
      v = resolveprop(js, js_expr(js));  // Yes. check condition
      if (is_err2(&v, &res)) goto done;  // Fail short on error
      if (!js_truthy(js, v)) break;      // Exit the loop if condition is false
    }
    js->pos = pos3, js->consumed = 1, js->flags |= F_LOOP;  // Execute the
    v = js_block_or_stmt(js);                               // loop body
    if (is_err2(&v, &res)) goto done;                       // Fail on error
    if (js->flags & F_BREAK) break;  // break was executed - exit the loop!
    js->flags = flags, js->pos = pos2, js->consumed = 1;  // Jump to final expr
    if (v7_next_token(js) != TOK_RPAREN) {                         // Is it specified?
      v = js_expr(js);                                    // Yes. Execute it
      if (is_err2(&v, &res)) goto done;  // On error, fail short
    }
  }
  js->pos = pos4, js->tok = TOK_SEMICOLON, js->consumed = 0;
done:
  if (exe) delscope(js);  // Exit scope
  js->flags = (uint8_t) (flags | (js->flags & F_RETURN));
  return res;
}

static jsval_t js_break(struct js *js) {
  if (js->flags & F_NOEXEC) {
  } else if (js->flags & F_SWITCH) {
    js->flags |= F_SWITCH_BREAK | F_NOEXEC;
  } else {
    if (!(js->flags & F_LOOP)) return js_mkerr(js, "not in loop or switch");
    js->flags |= F_BREAK | F_NOEXEC;
  }
  js->consumed = 1;
  return js_mkundef();
}

static jsval_t js_continue(struct js *js) {
  if (js->flags & F_NOEXEC) {
  } else {
    if (!(js->flags & F_LOOP)) return js_mkerr(js, "not in loop");
    js->flags |= F_NOEXEC;
  }
  js->consumed = 1;
  return js_mkundef();
}

static jsval_t js_return(struct js *js) {
  uint8_t exe = !(js->flags & F_NOEXEC);
  js->consumed = 1;
  if (exe && !(js->flags & F_CALL)) return js_mkerr(js, "not in func");
  if (v7_next_token(js) == TOK_SEMICOLON) return js_mkundef();
  jsval_t res = resolveprop(js, js_expr(js));
  if (exe) {
    js->pos = js->clen;     // Shift to the end - exit the code snippet
    js->flags |= F_RETURN;  // Tell caller we've executed
  }
  return resolveprop(js, res);
}

/* Switch sections are parsed once without execution, then executed from the
 * first matching case (or default) with ordinary fall-through semantics. */
static jsval_t js_switch(struct js *js) {
  enum { MAX_SWITCH_SECTIONS = 64 };
  struct section { jsval_t label; jsoff_t label_start, start, end; uint8_t is_default; } sec[MAX_SWITCH_SECTIONS];
  int count = 0, selected = -1, default_index = -1, i;
  uint8_t flags = js->flags;
  jsval_t res = js_mkundef(), discriminant;
  jsoff_t close_pos = 0;

  js->consumed = 1;
  if (v7_next_token(js) != TOK_LPAREN) return js_mkerr(js, "'(' expected after switch");
  js->consumed = 1;
  discriminant = resolveprop(js, js_expr(js));
  if (is_err(discriminant)) return discriminant;
  if (v7_next_token(js) != TOK_RPAREN) return js_mkerr(js, "')' expected after switch expression");
  js->consumed = 1;
  if (v7_next_token(js) != TOK_LBRACE) return js_mkerr(js, "'{' expected after switch expression");
  js->consumed = 1;

  /* First pass: record each section boundary and evaluate labels in no-exec
   * mode. This avoids running statements twice and supports nested blocks. */
  js->flags = (uint8_t) (flags | F_NOEXEC | F_SWITCH);
  while (v7_next_token(js) != TOK_RBRACE && v7_next_token(js) != TOK_EOF) {
    uint8_t tok = v7_next_token(js);
    jsval_t label = js_mkundef();
    jsoff_t label_start = 0;
    uint8_t is_default = 0;
    if (tok == TOK_CASE) {
      js->consumed = 1;
      label_start = js->pos;
      label = js_expr(js);
      if (is_err(label)) { res = label; js->flags = flags; return res; }
      if (v7_next_token(js) != TOK_COLON) { res = js_mkerr(js, "':' expected after case"); js->flags = flags; return res; }
      js->consumed = 1;
    } else if (tok == TOK_DEFAULT) {
      js->consumed = 1;
      is_default = 1;
      if (default_index >= 0) { res = js_mkerr(js, "duplicate default label"); js->flags = flags; return res; }
      if (v7_next_token(js) != TOK_COLON) { res = js_mkerr(js, "':' expected after default"); js->flags = flags; return res; }
      js->consumed = 1;
    } else {
      res = js_mkerr(js, "case or default expected in switch"); js->flags = flags; return res;
    }
    if (count >= MAX_SWITCH_SECTIONS) { res = js_mkerr(js, "too many switch sections"); js->flags = flags; return res; }
    sec[count].label = label;
    sec[count].label_start = label_start;
    sec[count].is_default = is_default;
    sec[count].start = js->pos;
    sec[count].end = js->pos;
    if (is_default) default_index = count;
    count++;
    while (v7_next_token(js) != TOK_CASE && v7_next_token(js) != TOK_DEFAULT &&
           v7_next_token(js) != TOK_RBRACE && v7_next_token(js) != TOK_EOF) {
      jsoff_t before = js->pos;
      res = js_stmt(js);
      if (is_err(res)) { js->flags = flags; return res; }
      if (js->pos <= before && js->tok != TOK_EOF) { res = js_mkerr(js, "switch parser made no progress"); js->flags = flags; return res; }
    }
    sec[count - 1].end = js->toff;
  }
  if (v7_next_token(js) != TOK_RBRACE) { res = js_mkerr(js, "'}' expected to close switch"); js->flags = flags; return res; }
  close_pos = js->pos;

  /* Select the first matching case; comparisons must run outside parse-only mode. */
  res = js_mkundef();
  js->flags = flags;
  if (!(flags & F_NOEXEC)) {
    for (i = 0; i < count; i++) {
      if (!sec[i].is_default) {
        js->pos = sec[i].label_start; js->consumed = 1;
        jsval_t runtime_label = resolveprop(js, js_expr(js));
        if (is_err(runtime_label)) { res = runtime_label; js->flags = flags; return res; }
        jsval_t eq = do_op(js, TOK_EQ, discriminant, runtime_label);
        if (is_err(eq)) { res = eq; js->flags = flags; return res; }
        if (js_truthy(js, eq)) { selected = i; break; }
      }
    }
    if (selected < 0) selected = default_index;
  }

  if (selected >= 0 && !(flags & F_NOEXEC)) {
    for (i = selected; i < count; i++) {
      jsoff_t p = sec[i].start;
      js->flags = (uint8_t) ((flags | F_SWITCH) & (uint8_t) ~F_NOEXEC);
      js->pos = p;
      js->tok = TOK_COLON;
      js->consumed = 1;
      while (js->pos < sec[i].end) {
        jsoff_t before = js->pos;
        if (v7_next_token(js) == TOK_CASE || v7_next_token(js) == TOK_DEFAULT || v7_next_token(js) == TOK_RBRACE) break;
        res = js_stmt(js);
        if (is_err(res) || (js->flags & F_SWITCH_BREAK)) break;
        if (js->pos <= before) { res = js_mkerr(js, "switch execution made no progress"); break; }
      }
      if (is_err(res) || (js->flags & F_SWITCH_BREAK)) break;
    }
  }
  js->pos = close_pos;
  js->tok = TOK_RBRACE;
  js->consumed = 0;
  js->flags = flags;
  return res;
}

static jsval_t js_while(struct js *js) {
  uint8_t flags = js->flags;
  jsval_t res = js_mkundef(), v;
  jsoff_t cond_start, body_start, after_pos;
  bool execute = (flags & F_NOEXEC) == 0;
  unsigned long iterations = 0;

  js->consumed = 1; /* consume while */
  if (v7_next_token(js) != TOK_LPAREN) return js_mkerr(js, "'(' expected after while");
  js->consumed = 1;
  cond_start = js->pos;
  js->flags = (uint8_t) (flags | F_NOEXEC);
  v = js_expr(js); /* validate condition once, without running it */
  if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  if (v7_next_token(js) != TOK_RPAREN) { res = js_mkerr(js, "')' expected after while condition"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  js->consumed = 1;
  body_start = js->pos;
  v = js_block_or_stmt(js); /* validate body once, without running it */
  if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  after_pos = js->pos;

  if (execute) {
    for (;;) {
      js->flags = flags;
      js->pos = cond_start;
      js->consumed = 1;
      v = resolveprop(js, js_expr(js));
      if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (v7_next_token(js) != TOK_RPAREN) { res = js_mkerr(js, "')' expected after while condition"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      js->consumed = 1;
      if (!js_truthy(js, v)) break;

      js->flags = (uint8_t) (flags | F_LOOP);
      js->pos = body_start;
      js->consumed = 1;
      v = js_block_or_stmt(js);
      if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (js->flags & F_RETURN) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (js->flags & F_BREAK) break;
      if (++iterations > 1000000UL) { res = js_mkerr(js, "while iteration limit exceeded"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
    }
  }
  js->pos = after_pos;
  js->tok = TOK_SEMICOLON;
  js->consumed = 0;
  js->flags = (uint8_t) (flags | (js->flags & F_RETURN));
  return res;
}

static jsval_t js_do_while(struct js *js) {
  uint8_t flags = js->flags;
  jsval_t res = js_mkundef(), v;
  jsoff_t body_start, cond_start, after_pos;
  bool execute = (flags & F_NOEXEC) == 0;
  js->consumed = 1; /* consume do */
  body_start = js->pos;

  /* Parse the body and condition once without executing them, recording source
   * offsets so the body and condition can be reevaluated on each iteration. */
  js->flags = (uint8_t) (flags | F_NOEXEC | F_DO_BODY);
  v = js_block_or_stmt(js);
  if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  if (v7_next_token(js) == TOK_RBRACE || v7_next_token(js) == TOK_SEMICOLON) js->consumed = 1;
  if (v7_next_token(js) != TOK_WHILE) { res = js_mkerr(js, "'while' expected after do body"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  js->consumed = 1;
  if (v7_next_token(js) != TOK_LPAREN) { res = js_mkerr(js, "'(' expected after while"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  js->consumed = 1;
  cond_start = js->pos;
  v = js_expr(js);
  if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  if (v7_next_token(js) != TOK_RPAREN) { res = js_mkerr(js, "')' expected after do-while condition"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
  js->consumed = 1;
  if (v7_next_token(js) == TOK_SEMICOLON) js->consumed = 1;
  after_pos = js->pos;

  if (execute) {
    unsigned long iterations = 0;
    for (;;) {
      js->flags = (uint8_t) (flags | F_LOOP | F_DO_BODY);
      js->pos = body_start; js->consumed = 1;
      v = js_block_or_stmt(js);
      if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (v7_next_token(js) == TOK_RBRACE) js->consumed = 1;
      if (js->flags & F_RETURN) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (js->flags & F_BREAK) break;
      js->flags = (uint8_t) (flags | F_DO_BODY);
      js->pos = cond_start; js->consumed = 1;
      v = resolveprop(js, js_expr(js));
      if (is_err(v)) { res = v; js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      if (v7_next_token(js) != TOK_RPAREN) { res = js_mkerr(js, "')' expected after do-while condition"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
      js->consumed = 1;
      if (!js_truthy(js, v)) break;
      if (++iterations > 1000000UL) { res = js_mkerr(js, "do-while iteration limit exceeded"); js->flags = (uint8_t) (flags | (js->flags & F_RETURN)); return res; }
    }
  }
  js->pos = after_pos;
  js->tok = TOK_SEMICOLON;
  js->consumed = 0;
  js->flags = (uint8_t) (flags | (js->flags & F_RETURN));
  return res;
}


/* Compact class support: constructors, methods, static methods and prototype inheritance. */
static jsval_t js_class(struct js *js) {
  uint8_t flags=js->flags; const char *name; size_t name_len; jsval_t ctor,class_name,base_ctor=js_mkundef(),base_proto=js_mkundef();
  const char *method_names[64]; size_t method_lens[64]; jsval_t method_values[64]; uint8_t method_static[64]; unsigned method_count=0;
  js->consumed=1;
  if(v7_next_token(js)!=TOK_IDENTIFIER)return js_mkerr(js,"class name expected");
  name=js->code+js->toff;name_len=js->tlen;class_name=js_mkstr(js,name,name_len);js->consumed=1;
  if(v7_next_token(js)==TOK_IDENTIFIER && js->tlen==7 && memcmp(js->code+js->toff,"extends",7)==0){
    js->consumed=1;if(v7_next_token(js)!=TOK_IDENTIFIER)return js_mkerr(js,"base class name expected after extends");
    base_ctor=resolveprop(js,lookup(js,js->code+js->toff,js->tlen));if(vtype(base_ctor)!=T_FUNC)return js_mkerr(js,"extends target must be a function/class");
    base_proto=function_prototype_value(js,base_ctor);js->consumed=1;
  }
  if(v7_next_token(js)!=TOK_LBRACE)return js_mkerr(js,"'{' expected after class declaration");js->consumed=1;
  ctor=js_make_function(js,"() {}",5);if(is_err(ctor))return ctor;
  while(v7_next_token(js)!=TOK_RBRACE && v7_next_token(js)!=TOK_EOF){
    const char *method_name;size_t method_len;jsoff_t method_start,method_end;jsval_t method;int is_static=0;
    if(v7_next_token(js)!=TOK_IDENTIFIER)return js_mkerr(js,"class method name expected");
    method_name=js->code+js->toff;method_len=js->tlen;
    if(method_len==6 && memcmp(method_name,"static",6)==0){
      jsoff_t static_off=js->toff;js->consumed=1;
      if(v7_next_token(js)==TOK_IDENTIFIER){is_static=1;method_name=js->code+js->toff;method_len=js->tlen;}
      else {js->pos=static_off;js->toff=static_off;js->tok=TOK_IDENTIFIER;js->tlen=6;js->consumed=0;method_name=js->code+static_off;method_len=6;}
    }
    js->consumed=1;if(v7_next_token(js)!=TOK_LPAREN)return js_mkerr(js,"'(' expected after method name");method_start=js->toff;js->consumed=1;
    while(v7_next_token(js)!=TOK_RPAREN && v7_next_token(js)!=TOK_EOF){
      if(v7_next_token(js)==TOK_ELLIPSIS)js->consumed=1;
      if(v7_next_token(js)!=TOK_IDENTIFIER)return js_mkerr(js,"invalid method parameter list");js->consumed=1;
      if(v7_next_token(js)==TOK_ASSIGN){js->consumed=1;uint8_t save=js->flags;js->flags|=F_NOEXEC;(void)js_expr(js);js->flags=save;}
      if(v7_next_token(js)==TOK_RPAREN)break;if(v7_next_token(js)!=TOK_COMMA)return js_mkerr(js,"',' expected in parameters");js->consumed=1;
    }
    if(v7_next_token(js)!=TOK_RPAREN)return js_mkerr(js,"')' expected after parameters");js->consumed=1;
    if(v7_next_token(js)!=TOK_LBRACE)return js_mkerr(js,"'{' expected before method body");js->consumed=1;js->flags=(uint8_t)(flags|F_NOEXEC);
    {jsval_t parsed=js_block(js,false);if(is_err(parsed)){js->flags=flags;return parsed;}}
    if(v7_next_token(js)!=TOK_RBRACE){js->flags=flags;return js_mkerr(js,"'}' expected after method body");}method_end=js->pos;js->consumed=1;js->flags=flags;
    size_t method_len_src=(size_t)(method_end-method_start);char *method_src=(char*)malloc(method_len_src+1);if(!method_src)return js_mkerr(js,"out of memory building class method");
    memcpy(method_src,js->code+method_start,method_len_src);method_src[method_len_src]=0;
    if(!is_static && method_len==11 && memcmp(method_name,"constructor",11)==0 && vtype(base_ctor)==T_FUNC){
      for(size_t q=0;q+6<method_len_src;q++)if(!memcmp(method_src+q,"super(",6)){
        size_t j=q+6;int depth=1;char quote=0;for(;j<method_len_src&&depth;j++){char c=method_src[j];if(quote){if(c=='\\')j++;else if(c==quote)quote=0;continue;}if(c=='\''||c=='\"'||c=='`'){quote=c;continue;}if(c=='(')depth++;else if(c==')')depth--;}
        if(depth==0){size_t tail=method_len_src-j;method_src[q]='0';memmove(method_src+q+1,method_src+j,tail);method_len_src-=j-q-1;method_src[method_len_src]=0;}
      }
    }
    method=js_make_function(js,method_src,method_len_src);free(method_src);if(is_err(method))return method;
    if(!is_static && method_len==11 && memcmp(method_name,"constructor",11)==0)ctor=method;
    else {if(method_count>=64)return js_mkerr(js,"too many class methods");method_names[method_count]=method_name;method_lens[method_count]=method_len;method_values[method_count]=method;method_static[method_count]=(uint8_t)is_static;method_count++;}
    if(v7_next_token(js)==TOK_SEMICOLON)js->consumed=1;
  }
  if(v7_next_token(js)!=TOK_RBRACE)return js_mkerr(js,"'}' expected after class body");js->consumed=1;
  {jsval_t proto=function_prototype_value(js,ctor);if(vtype(base_proto)==T_OBJ){saveoff(js,(jsoff_t)(vdata(proto)+sizeof(jsoff_t)),(jsoff_t)vdata(base_proto));prop_set(js,ctor,"__dm_super",base_ctor);}prop_set(js,proto,"constructor",ctor);
   for(unsigned i=0;i<method_count;i++){jsval_t key=js_mkstr(js,method_names[i],method_lens[i]);if(method_static[i]){int fi=function_index(js,(jsoff_t)vdata(ctor));if(fi<0)return js_mkerr(js,"invalid class constructor");jsval_t set=setprop(js,mkval(T_OBJ,js->function_props[fi]),key,method_values[i]);if(is_err(set))return set;}else{jsval_t set=setprop(js,proto,key,method_values[i]);if(is_err(set))return set;}}}
  if(!(flags&F_NOEXEC)){if(lkp(js,js->scope,name,name_len)!=0)return js_mkerr(js,"'%.*s' already declared",(int)name_len,name);if(is_err(setprop(js,js->scope,class_name,ctor)))return js_mkerr(js,"could not define class");}
  if (v7_next_token(js) == TOK_SEMICOLON) js->consumed = 1;
  return ctor;
}
/* Capture a braced statement body while parsing in F_NOEXEC mode. The returned
 * offsets exclude the braces, allowing the already-validated body to execute
 * later without re-running its parser against the surrounding statement. */
static jsval_t js_capture_body(struct js *js, jsoff_t *start, jsoff_t *end) {
  jsval_t res;
  if (v7_next_token(js) != TOK_LBRACE) return js_mkerr(js, "'{' expected");
  js->consumed = 1;
  *start = js->pos;
  res = js_block(js, false);
  if (is_err(res)) return res;
  if (v7_next_token(js) != TOK_RBRACE) return js_mkerr(js, "'}' expected");
  *end = js->toff;
  js->consumed = 1;
  return js_mkundef();
}

/* Evaluate a captured source range while preserving the outer parser. This is
 * ordinary C control flow; exceptions never use setjmp/longjmp. */
static jsval_t js_eval_slice(struct js *js, jsoff_t start, jsoff_t end) {
  const char *code = js->code;
  jsoff_t clen = js->clen, pos = js->pos, toff = js->toff, tlen = js->tlen;
  uint8_t tok = js->tok, consumed = js->consumed, flags = js->flags;
  void *cstk = js->cstk;
  jsval_t res;
  res = js_eval(js, code + start, (size_t) (end - start));
  js->code = code; js->clen = clen; js->pos = pos;
  js->toff = toff; js->tlen = tlen; js->tok = tok;
  js->consumed = consumed; js->flags = flags; js->cstk = cstk;
  return res;
}

static jsval_t js_try(struct js *js) {
  uint8_t flags = js->flags;
  jsoff_t try_start = 0, try_end = 0, catch_start = 0, catch_end = 0;
  jsoff_t finally_start = 0, finally_end = 0, stmt_end = 0, stmt_to = 0;
  jsoff_t probe_pos = 0, probe_to = 0; uint8_t probe_tok = TOK_ERR;
  const char *catch_name = NULL;
  size_t catch_len = 0;
  bool has_catch = false, has_finally = false;
  jsval_t parsed, res = js_mkundef(), caught;
  bool pending_throw = false;

  js->consumed = 1;
  js->flags = (uint8_t) (flags | F_NOEXEC);
  parsed = js_capture_body(js, &try_start, &try_end);
  if (is_err(parsed)) goto parse_error;
  stmt_end = js->pos; stmt_to = js->toff;

  if (v7_next_token(js) == TOK_CATCH) {
    js->consumed = 1;
    if (v7_next_token(js) != TOK_LPAREN) { parsed = js_mkerr(js, "'(' expected after catch"); goto parse_error; }
    js->consumed = 1;
    if (v7_next_token(js) != TOK_IDENTIFIER) { parsed = js_mkerr(js, "catch binding expected"); goto parse_error; }
    catch_name = js->code + js->toff;
    catch_len = js->tlen;
    js->consumed = 1;
    if (v7_next_token(js) != TOK_RPAREN) { parsed = js_mkerr(js, "')' expected after catch binding"); goto parse_error; }
    js->consumed = 1;
    parsed = js_capture_body(js, &catch_start, &catch_end);
    if (is_err(parsed)) goto parse_error;
    has_catch = true; stmt_end = js->pos; stmt_to = js->toff;
  }
  if (v7_next_token(js) == TOK_FINALLY) {
    js->consumed = 1;
    parsed = js_capture_body(js, &finally_start, &finally_end);
    if (is_err(parsed)) goto parse_error;
    has_finally = true; stmt_end = js->pos; stmt_to = js->toff;
  }
  probe_tok = v7_next_token(js); probe_pos = js->pos; probe_to = js->toff;
  if (!has_catch && !has_finally) {
    parsed = js_mkerr(js, "try requires catch or finally");
    goto parse_error;
  }

  js->flags = flags;
  res = js_eval_slice(js, try_start, try_end);
  if (is_err(res) && has_catch) {
    pending_throw = js->has_thrown != 0;
    caught = pending_throw ? js->thrown : js_mkstr(js, js->errmsg, strlen(js->errmsg));
    js->has_thrown = 0;
    mkscope(js);
    {
      jsval_t binding = setprop(js, js->scope,
                                js_mkstr(js, catch_name, catch_len), caught);
      if (is_err(binding)) res = binding;
      else res = js_eval_slice(js, catch_start, catch_end);
    }
    delscope(js);
    pending_throw = is_err(res) && js->has_thrown;
  } else {
    pending_throw = is_err(res) && js->has_thrown;
  }
  if (has_finally) {
    jsval_t final_res = js_eval_slice(js, finally_start, finally_end);
    if (is_err(final_res)) res = final_res;
  }
  if (pending_throw) js->has_thrown = 1;
  js->flags = flags;
  /* Clause detection may have looked one token past the statement. Restore the
   * closing-brace cursor so js_stmt consumes the following semicolon normally. */
  if (probe_tok == TOK_SEMICOLON) {
    js->pos = probe_pos; js->toff = probe_to; js->tok = TOK_SEMICOLON; js->consumed = 1;
  } else {
    js->pos = stmt_end; js->toff = stmt_to; js->tok = TOK_RBRACE; js->consumed = 1;
  }
  return res;

parse_error:
  js->flags = flags;
  return parsed;
}

static jsval_t js_throw(struct js *js) {
  jsval_t value;
  js->consumed = 1;
  value = resolveprop(js, js_expr(js));
  if (is_err(value)) return value;
  if (!(js->flags & F_NOEXEC)) {
    char message[96];
    size_t n = tostr(js, value, message, sizeof(message));
    if (n >= sizeof(message)) n = sizeof(message) - 1;
    message[n] = '\0';
    (void) snprintf(js->errmsg, sizeof(js->errmsg), "Uncaught: %s", message);
    js->thrown = value;
    js->has_thrown = 1;
  }
  return (js->flags & F_NOEXEC) ? js_mkundef() : mkval(T_ERR, 0);
}

static jsval_t js_function_declaration(struct js *js) {
  const char *name; size_t name_len; jsval_t fn; uint8_t flags=js->flags;
  js->consumed=1;
  if(v7_next_token(js)!=TOK_IDENTIFIER)return js_mkerr(js,"function declaration requires a name");
  name=js->code+js->toff;name_len=js->tlen;js->consumed=1;
  fn=js_func_literal(js);if(is_err(fn))return fn;
  if(!(flags&F_NOEXEC)){jsval_t set=setprop(js,js->scope,js_mkstr(js,name,name_len),fn);if(is_err(set))return set;}
  return fn;
}

static jsval_t js_stmt(struct js *js) {
  jsval_t res;
  uint8_t stmt_tok;
  // jsoff_t pos = js->pos - js->tlen;
  if(js->brk>js->gct)js_gc(js);
  /* Wastegate collects only at safe statement boundaries. */
  if(js->nogc!=(jsoff_t)~0&&js->size>js->brk&&js->size-js->brk<=512U){jsoff_t before=js->brk;js_gc(js);if(js->brk<before)js->wastegate_vents++;}
  stmt_tok = v7_next_token(js);
  switch (stmt_tok) {  // clang-format off
    case TOK_CATCH: case TOK_DEFAULT: case TOK_FINALLY:
    case TOK_INSTANCEOF:
    case TOK_WITH:
      res = js->strict_mode ? js_mkerr(js, "with is not allowed in strict mode") : js_mkerr(js, "'%.*s' not implemented", (int) js->tlen, js->code + js->toff);
      break;
    case TOK_YIELD:
      res = js_mkerr(js, "'%.*s' not implemented", (int) js->tlen, js->code + js->toff);
      break;
    case TOK_CLASS:      res = js_class(js); break;
    case TOK_FUNC:       res = js_function_declaration(js); break;
    case TOK_ASYNC: {
      /* Async function declarations and async-arrow expression statements share
       * the same function representation and Promise wrapper. */
      jsoff_t saved_pos = js->pos;
      uint8_t saved_tok = js->tok, saved_consumed = js->consumed;
      js->consumed = 1;
      uint8_t next_tok = v7_next_token(js);
      js->pos = saved_pos; js->tok = saved_tok; js->consumed = saved_consumed;
      if (next_tok != TOK_FUNC) {
        res = resolveprop(js, js_expr(js));
        if (v7_next_token(js) == TOK_SEMICOLON) js->consumed = 1;
        else js->consumed = 0;
        break;
      }
      jsval_t fn_name = js_mkundef();
      int fi = -1;
      js->consumed = 1;
      if (v7_next_token(js) != TOK_FUNC) {
        res = js_mkerr(js, "async declaration requires function");
        break;
      }
      js->consumed = 1;
      if (v7_next_token(js) != TOK_IDENTIFIER) {
        res = js_mkerr(js, "async function declaration requires a name");
        break;
      }
      if (!(js->flags & F_NOEXEC))
        fn_name = js_mkstr(js, js->code + js->toff, js->tlen);
      js->consumed = 1;
      res = js_func_literal(js);      if (!is_err(res) && !(js->flags & F_NOEXEC)) {
        fi = function_index(js, (jsoff_t) vdata(res));
        if (fi >= 0) js->function_async[fi] = 1;
        res = setprop(js, js->scope, fn_name, res);
      }
      break;
    }
    case TOK_THROW:      res = js_throw(js); break;
    case TOK_TRY:        res = js_try(js); break;
    case TOK_CASE: case TOK_IN:
      res = js_mkerr(js, "'%.*s' not implemented", (int) js->tlen, js->code + js->toff); break;
    case TOK_SWITCH:     res = js_switch(js); break;
    case TOK_DO:        res = js_do_while(js); break;
    case TOK_CONTINUE:  res = js_continue(js); break;
    case TOK_BREAK:     res = js_break(js); break;
    case TOK_LET: case TOK_VAR: case TOK_CONST: res = js_let(js); break;
    case TOK_IF:        res = js_if(js); break;
    case TOK_WHILE:     res = js_while(js); break;
    case TOK_LBRACE:    res = js_block(js, !(js->flags & F_NOEXEC)); break;
    case TOK_FOR:       res = js_for(js); break; // 25222 -> 27660
    case TOK_RETURN:    res = js_return(js); break;
    default:            res = resolveprop(js, js_expr(js)); break;
  }
  //printf("STMT [%.*s] -> %s, tok %d, flags %d\n", (int) (js->pos - pos), &js->code[pos], js_str(js, res), v7_next_token(js), js->flags);
  if (stmt_tok != TOK_TRY && stmt_tok != TOK_CLASS && stmt_tok != TOK_ASYNC && stmt_tok != TOK_FUNC) {
    uint8_t next = v7_next_token(js);
    bool asi_boundary = next != TOK_SEMICOLON && next != TOK_EOF && next != TOK_RBRACE &&
      !((js->flags & F_SWITCH) && (next == TOK_CASE || next == TOK_DEFAULT)) &&
      !((js->flags & F_DO_BODY) && next == TOK_WHILE) &&
      dm_linebreak_before(js->code, (size_t) js->toff);
    if (next != TOK_SEMICOLON && next != TOK_EOF && next != TOK_RBRACE &&
        !((js->flags & F_SWITCH) && (next == TOK_CASE || next == TOK_DEFAULT)) &&
        !((js->flags & F_DO_BODY) && next == TOK_WHILE) && !asi_boundary)
      return js_mkerr(js, "; expected");
    js->consumed = (asi_boundary || next == TOK_RBRACE ||
                    ((js->flags & F_DO_BODY) && next == TOK_WHILE)) ? 0 : 1;
  }
  // clang-format on
  return res;
}

static int dm_collection_find(struct js *js, jsval_t c, jsval_t key) {
  jsval_t keys=prop_value(js,c,"__dm_keys");if(!dm_array(js,keys))return -1;
  unsigned long n=dm_array_length(js,keys);
  for(unsigned long i=0;i<n;i++){char k[32];(void)snprintf(k,sizeof(k),"%lu",i);if(lkp(js,keys,k,strlen(k))==0)continue;jsval_t old=prop_value(js,keys,k);if(dm_array_equal(js,old,key))return (int)i;}
  return -1;
}
static jsval_t dm_collection_set(struct js *js, jsval_t *args, int nargs) {
  jsval_t c=js->this_val;if(nargs<1)return js_mkerr(js,"collection.set/add requires a key/value");
  int weak=vtype(prop_value(js,c,"__dm_weak"))==T_BOOL&&vdata(prop_value(js,c,"__dm_weak"));
  if(weak && vtype(args[0])!=T_OBJ && vtype(args[0])!=T_FUNC)return js_mkerr(js,"WeakMap/WeakSet keys must be objects");
  jsval_t keys=prop_value(js,c,"__dm_keys"),vals=prop_value(js,c,"__dm_values");int setmode=vtype(prop_value(js,c,"__dm_setmode"))==T_BOOL&&vdata(prop_value(js,c,"__dm_setmode"));
  int at=dm_collection_find(js,c,args[0]);
  if(at<0){at=(int)dm_array_length(js,keys);dm_array_store(js,keys,(unsigned long)at,args[0]);prop_set(js,c,"size",tov((double)at+1));}
  if(!setmode){if(nargs<2)return js_mkerr(js,"Map.set requires a value");dm_array_store(js,vals,(unsigned long)at,args[1]);}
  return c;
}
static jsval_t dm_collection_get(struct js *js, jsval_t *args, int nargs) {
  jsval_t c=js->this_val;if(!nargs)return js_mkundef();int at=dm_collection_find(js,c,args[0]);if(at<0)return js_mkundef();
  if(vtype(prop_value(js,c,"__dm_setmode"))==T_BOOL&&vdata(prop_value(js,c,"__dm_setmode")))return args[0];
  jsval_t vals=prop_value(js,c,"__dm_values");char k[32];(void)snprintf(k,sizeof(k),"%d",at);return prop_value(js,vals,k);
}
static jsval_t dm_collection_has(struct js *js, jsval_t *args, int nargs) {
  return nargs>0&&dm_collection_find(js,js->this_val,args[0])>=0?js_mktrue():js_mkfalse();
}
static jsval_t dm_collection_delete(struct js *js, jsval_t *args, int nargs) {
  jsval_t c=js->this_val;if(!nargs)return js_mkfalse();int at=dm_collection_find(js,c,args[0]);if(at<0)return js_mkfalse();
  jsval_t keys=prop_value(js,c,"__dm_keys"),vals=prop_value(js,c,"__dm_values");char k[32];(void)snprintf(k,sizeof(k),"%d",at);prop_set(js,keys,k,mkval(T_HOLE,0));if(dm_array(js,vals))prop_set(js,vals,k,mkval(T_HOLE,0));
  double n=tod(prop_value(js,c,"size"));if(n>0)prop_set(js,c,"size",tov(n-1));return js_mktrue();
}
static jsval_t dm_collection_clear(struct js *js, jsval_t *args, int nargs) {
  (void)args;(void)nargs;jsval_t c=js->this_val;prop_set(js,c,"__dm_keys",dm_array_new(js));prop_set(js,c,"__dm_values",dm_array_new(js));prop_set(js,c,"size",tov(0));return js_mkundef();
}

enum { DM_COLLECTION_KEYS, DM_COLLECTION_VALUES, DM_COLLECTION_ENTRIES };

static jsval_t dm_collection_snapshot(struct js *js, jsval_t c, int mode) {
  jsval_t keys = prop_value(js, c, "__dm_keys");
  jsval_t vals = prop_value(js, c, "__dm_values");
  jsval_t setmode_val = prop_value(js, c, "__dm_setmode");
  int setmode = vtype(setmode_val) == T_BOOL && vdata(setmode_val);
  if (!dm_array(js, keys)) return js_mkerr(js, "invalid Map/Set storage");
  unsigned long n = dm_array_length(js, keys), used = 0;
  jsval_t out = dm_array_new(js);
  for (unsigned long i = 0; i < n; i++) {
    char index[32];
    (void) snprintf(index, sizeof(index), "%lu", i);
    if (lkp(js, keys, index, strlen(index)) == 0) continue;
    jsval_t key = prop_value(js, keys, index);
    jsval_t value = key;
    if (!setmode && mode != DM_COLLECTION_KEYS) {
      value = prop_value(js, vals, index);
      if (vtype(value) == T_HOLE) value = js_mkundef();
    }
    if (mode == DM_COLLECTION_ENTRIES) {
      jsval_t pair = dm_array_new(js);
      dm_array_store(js, pair, 0, key);
      dm_array_store(js, pair, 1, setmode ? key : value);
      value = pair;
    }
    dm_array_store(js, out, used++, value);
  }
  return out;
}

static jsval_t dm_collection_keys(struct js *js, jsval_t *a, int n) {
  (void) a; (void) n;
  return dm_collection_snapshot(js, js->this_val, DM_COLLECTION_KEYS);
}
static jsval_t dm_collection_values(struct js *js, jsval_t *a, int n) {
  (void) a; (void) n;
  return dm_collection_snapshot(js, js->this_val, DM_COLLECTION_VALUES);
}
static jsval_t dm_collection_entries(struct js *js, jsval_t *a, int n) {
  (void) a; (void) n;
  return dm_collection_snapshot(js, js->this_val, DM_COLLECTION_ENTRIES);
}

static jsval_t dm_forof_values(struct js *js, jsval_t iterable) {
  if (dm_array(js, iterable)) return iterable;
  if (vtype(iterable) == T_STR) {
    size_t len = 0;
    const char *s = js_getstr(js, iterable, &len);
    jsval_t out = dm_array_new(js);
    for (size_t i = 0; i < len; i++)
      dm_array_store(js, out, (unsigned long)i, js_mkstr(js, s + i, 1));
    return out;
  }
  if (vtype(iterable) == T_OBJ) {
    jsval_t keys = prop_value(js, iterable, "__dm_keys");
    jsval_t setmode = prop_value(js, iterable, "__dm_setmode");
    jsval_t weak = prop_value(js, iterable, "__dm_weak");
    if (dm_array(js, keys) && vtype(setmode) == T_BOOL &&
        vtype(weak) == T_BOOL && !vdata(weak)) {
      return dm_collection_snapshot(js, iterable,
             vdata(setmode) ? DM_COLLECTION_VALUES : DM_COLLECTION_ENTRIES);
    }
  }
  return js_mkerr(js, "right side of for...of must be an array, string, Map or Set");
}
static jsval_t dm_symbol_native(struct js *js, jsval_t *args, int nargs) {
  static unsigned long serial=1;jsval_t sym=mkobj(js,0);char id[48];(void)snprintf(id,sizeof(id),"Symbol(%lu)",serial++);prop_set(js,sym,"__dm_symbol",js_mktrue());prop_set(js,sym,"__dm_symbol_id",js_mkstr(js,id,strlen(id)));if(nargs>0)prop_set(js,sym,"description",args[0]);return sym;
}
static jsval_t dm_object_collect(struct js *js, jsval_t *args, int nargs, int mode) {
  if (nargs < 1) return js_mkerr(js, "Object method requires a value");
  jsval_t obj = args[0];
  if (vtype(obj) == T_FUNC) { int fi = function_index(js, (jsoff_t) vdata(obj)); if (fi < 0) return dm_array_new(js); obj = mkval(T_OBJ, js->function_props[fi]); }
  if (vtype(obj) != T_OBJ) return js_mkerr(js, "Object method requires an object");
  jsval_t out = dm_array_new(js); unsigned long index = 0;
  if (dm_array(js, obj)) {
    /* Array indices are enumerable in ascending order; length and methods are
     * not enumerable own properties in JavaScript. */
    unsigned long length = dm_array_length(js, obj);
    for (unsigned long i = 0; i < length; i++) {
      char key[32]; (void) snprintf(key, sizeof(key), "%lu", i);
      jsoff_t off = lkp(js, obj, key, strlen(key));
      if (off == 0 || (mode != 3 && !(prop_flags(js, off) & V7_PROP_ENUMERABLE))) continue; /* skip holes/non-enumerables */
      jsval_t keyval = js_mkstr(js, key, strlen(key));
      jsval_t value = loadval(js, off + sizeof(jsoff_t) * 2);
      if (mode == 0) dm_array_store(js, out, index++, keyval);
      else if (mode == 1) dm_array_store(js, out, index++, value);
      else { jsval_t pair = dm_array_new(js); dm_array_store(js, pair, 0, keyval); dm_array_store(js, pair, 1, value); dm_array_store(js, out, index++, pair); }
    }
    return out;
  }
  jsoff_t next = loadoff(js, (jsoff_t) vdata(obj)) & ~3U;
  while (next != 0 && next < js->brk) {
    jsoff_t keyoff = loadoff(js, next + sizeof(jsoff_t));
    jsval_t value = loadval(js, next + sizeof(jsoff_t) * 2);
    jsoff_t following = loadoff(js, next) & ~3U;
    size_t keylen = offtolen(loadoff(js, keyoff));
    const char *key = (const char *) &js->mem[keyoff + sizeof(jsoff_t)];
    if (vtype(value) != T_HOLE && !(keylen >= 5 && memcmp(key, "__dm_", 5) == 0) && !(keylen >= 5 && memcmp(key, "__v7_", 5) == 0) && (mode == 3 || (prop_flags(js, next) & V7_PROP_ENUMERABLE))) {
      jsval_t keyval = mkval(T_STR, keyoff);
      if (mode == 0) dm_array_store(js, out, index++, keyval);
      else if (mode == 1) dm_array_store(js, out, index++, value);
      else { jsval_t pair = dm_array_new(js); dm_array_store(js, pair, 0, keyval); dm_array_store(js, pair, 1, value); dm_array_store(js, out, index++, pair); }
    }
    next = following;
  }
  /* The compact property list prepends writes. Reverse results to recover
   * insertion order for ordinary objects and functions. */
  for (unsigned long i = 0; i < index / 2; i++) {
    char left[32], right[32];
    unsigned long j = index - 1 - i;
    (void) snprintf(left, sizeof(left), "%lu", i);
    (void) snprintf(right, sizeof(right), "%lu", j);
    jsval_t a = prop_value(js, out, left), b = prop_value(js, out, right);
    dm_array_store(js, out, i, b);
    dm_array_store(js, out, j, a);
  }
  return out;
}
static jsval_t dm_object_keys(struct js *js, jsval_t *args, int nargs) { return dm_object_collect(js, args, nargs, 0); }
static jsval_t dm_object_names(struct js *js, jsval_t *args, int nargs) { return dm_object_collect(js, args, nargs, 3); }
static jsval_t dm_object_values(struct js *js, jsval_t *args, int nargs) { return dm_object_collect(js, args, nargs, 1); }
static jsval_t dm_object_entries(struct js *js, jsval_t *args, int nargs) { return dm_object_collect(js, args, nargs, 2); }
static jsval_t dm_object_hasown(struct js *js, jsval_t *args, int nargs) {
  if (nargs < 2) return js_mkfalse();
  jsval_t obj = args[0]; if (vtype(obj) == T_FUNC) { int fi = function_index(js, (jsoff_t) vdata(obj)); if (fi < 0) return js_mkfalse(); obj = mkval(T_OBJ, js->function_props[fi]); }
  if (vtype(obj) != T_OBJ) return js_mkfalse();
  char keybuf[64]; const char *key = NULL; size_t keylen = 0;
  if (vtype(args[1]) == T_STR) key = js_getstr(js, args[1], &keylen);
  else { keylen = tostr(js, args[1], keybuf, sizeof(keybuf)); if (keylen >= sizeof(keybuf)) keylen = sizeof(keybuf) - 1; key = keybuf; }
  return mkval(T_BOOL, key != NULL && lkp(js, obj, key, keylen) != 0);
}
static jsval_t dm_object_is(struct js *js, jsval_t *args, int nargs) {
  (void)js;
  if (nargs < 2) return js_mkfalse();
  jsval_t a = args[0], b = args[1];
  if (vtype(a) != vtype(b)) return js_mkfalse();
  if (vtype(a) == T_NUM) {
    double x = tod(a), y = tod(b);
    return mkval(T_BOOL, (isnan(x) && isnan(y)) || (x == y && (x != 0.0 || signbit(x) == signbit(y))));
  }
  if (vtype(a) == T_STR) {
    size_t an = 0, bn = 0; const char *as = js_getstr(js, a, &an), *bs = js_getstr(js, b, &bn);
    return mkval(T_BOOL, as && bs && an == bn && memcmp(as, bs, an) == 0);
  }
  return mkval(T_BOOL, a == b);
}

static jsval_t dm_object_assign(struct js *js, jsval_t *args, int nargs) {
  if (nargs < 1 || vtype(args[0]) != T_OBJ) return js_mkerr(js, "Object.assign target must be an object");
  jsval_t target = args[0];
  for (int a = 1; a < nargs; a++) {
    jsval_t source = args[a]; if (vtype(source) == T_FUNC) { int fi = function_index(js, (jsoff_t) vdata(source)); if (fi < 0) continue; source = mkval(T_OBJ, js->function_props[fi]); }
    if (vtype(source) != T_OBJ) continue;
    jsoff_t next = loadoff(js, (jsoff_t) vdata(source)) & ~3U;
    while (next != 0 && next < js->brk) {
      jsoff_t keyoff = loadoff(js, next + sizeof(jsoff_t)); jsval_t value = loadval(js, next + sizeof(jsoff_t) * 2); jsoff_t following = loadoff(js, next) & ~3U;
      size_t keylen = offtolen(loadoff(js, keyoff)); const char *key = (const char *) &js->mem[keyoff + sizeof(jsoff_t)];
      if (vtype(value) != T_HOLE && (prop_flags(js,next)&V7_PROP_ENUMERABLE) && !(keylen >= 5 && memcmp(key, "__dm_", 5) == 0) && !(keylen >= 5 && memcmp(key, "__v7_", 5) == 0)) (void) setprop(js, target, mkval(T_STR, keyoff), value);
      next = following;
    }
  }
  return target;
}
static jsval_t dm_object_fromentries(struct js *js, jsval_t *args, int nargs) {
  if(nargs<1||!dm_array(js,args[0]))return js_mkerr(js,"Object.fromEntries requires an array of entries");jsval_t out=mkobj(js,0);unsigned long n=dm_array_length(js,args[0]);
  for(unsigned long i=0;i<n;i++){char k[32];(void)snprintf(k,sizeof(k),"%lu",i);if(!lkp(js,args[0],k,strlen(k)))continue;jsval_t pair=prop_value(js,args[0],k);if(!dm_array(js,pair)||dm_array_length(js,pair)<2)return js_mkerr(js,"Object.fromEntries entries must be key/value arrays");jsval_t keyval=prop_value(js,pair,"0"),value=prop_value(js,pair,"1");char keybuf[64];const char *key=NULL;size_t keylen=0;if(vtype(keyval)==T_STR)key=js_getstr(js,keyval,&keylen);else{keylen=tostr(js,keyval,keybuf,sizeof(keybuf));if(keylen>=sizeof(keybuf))keylen=sizeof(keybuf)-1;key=keybuf;}if(key)(void)setprop(js,out,js_mkstr(js,key,keylen),value);}
  return out;
}
/* ES2024 Object.groupBy subset for array input. */
static jsval_t dm_object_groupby(struct js *js,jsval_t *args,int nargs){
 if(nargs<2||!dm_array(js,args[0])||vtype(args[1])!=T_FUNC)return js_mkerr(js,"Object.groupBy requires an array and callback");
 jsval_t out=mkobj(js,0),input=args[0];unsigned long n=dm_array_length(js,input);
 for(unsigned long i=0;i<n;i++){char ix[32],kb[128];(void)snprintf(ix,sizeof(ix),"%lu",i);if(!lkp(js,input,ix,strlen(ix)))continue;
  jsval_t item=prop_value(js,input,ix),kv=dm_array_callback(js,args[1],item,i,input);if(is_err(kv))return kv;kv=resolveprop(js,kv);
  const char *key;size_t kl;if(vtype(kv)==T_STR)key=js_getstr(js,kv,&kl);else{kl=tostr(js,kv,kb,sizeof(kb));if(kl>=sizeof(kb))kl=sizeof(kb)-1;key=kb;}
  if(!key)return js_mkerr(js,"Object.groupBy could not convert group key");
  jsval_t group;if(!lkp(js,out,key,kl)){group=dm_array_new(js);if(is_err(setprop(js,out,js_mkstr(js,key,kl),group)))return js_mkerr(js,"Object.groupBy failed");}else group=prop_value(js,out,key);
  dm_array_store(js,group,dm_array_length(js,group),item);
 }
 return out;
}
static jsval_t dm_object_create(struct js *js, jsval_t *args, int nargs) {
  if (nargs < 1 || vtype(args[0]) != T_OBJ) return js_mkerr(js, "Object.create currently requires an object prototype");
  return mkobj(js, (jsoff_t) vdata(args[0]));
}


static jsval_t dm_object_descriptor(struct js *js, jsval_t *a, int n) {
  if (n < 2 || vtype(a[0]) != T_OBJ) return js_mkundef();
  char keybuf[96]; const char *key = NULL; size_t keylen = 0;
  if (vtype(a[1]) == T_STR) key = js_getstr(js, a[1], &keylen);
  else { keylen = tostr(js, a[1], keybuf, sizeof(keybuf)); if (keylen >= sizeof(keybuf)) keylen = sizeof(keybuf)-1; key = keybuf; }
  if (!key) return js_mkundef();
  jsoff_t off = lkp(js, a[0], key, keylen); if (!off) return js_mkundef();
  uint32_t flags = prop_flags(js, off); jsval_t out = mkobj(js, 0);
  if (flags & V7_PROP_ACCESSOR) {
    prop_set(js, out, "get", prop_getter(js, off));
    prop_set(js, out, "set", prop_setter(js, off));
  } else {
    prop_set(js, out, "value", loadval(js, off + sizeof(jsoff_t)*2));
    prop_set(js, out, "writable", mkval(T_BOOL, !!(flags & V7_PROP_WRITABLE)));
  }
  prop_set(js, out, "enumerable", mkval(T_BOOL, !!(flags & V7_PROP_ENUMERABLE)));
  prop_set(js, out, "configurable", mkval(T_BOOL, !!(flags & V7_PROP_CONFIGURABLE)));
  return out;
}
static jsval_t dm_object_defineproperty(struct js *js, jsval_t *a, int n) {
  if (n < 3 || vtype(a[0]) != T_OBJ || vtype(a[2]) != T_OBJ)
    return js_mkerr(js, "Object.defineProperty requires an object, key, and descriptor");
  char keybuf[96]; const char *key = NULL; size_t keylen = 0;
  if (vtype(a[1]) == T_STR) key = js_getstr(js, a[1], &keylen);  else { keylen = tostr(js, a[1], keybuf, sizeof(keybuf)); if (keylen >= sizeof(keybuf)) keylen = sizeof(keybuf)-1; key = keybuf; }
  if (!key) return js_mkerr(js, "invalid property key");

  jsoff_t off = lkp(js, a[0], key, keylen);
  if (!off && vtype(prop_value(js, a[0], "__v7_extensible")) == T_BOOL && !vdata(prop_value(js, a[0], "__v7_extensible")))
    return js_mkerr(js, "object is not extensible");

  bool has_value = lkp(js, a[2], "value", 5) != 0;
  bool has_writable = lkp(js, a[2], "writable", 8) != 0;
  bool has_get = lkp(js, a[2], "get", 3) != 0;
  bool has_set = lkp(js, a[2], "set", 3) != 0;
  bool existing_accessor = off && (prop_flags(js, off) & V7_PROP_ACCESSOR);
  bool accessor = has_get || has_set || (!has_value && !has_writable && existing_accessor);

  jsval_t old_value = off ? loadval(js, off + sizeof(jsoff_t)*2) : js_mkundef();
  jsval_t value = has_value ? prop_value(js, a[2], "value") : old_value;
  jsval_t writable = has_writable ? prop_value(js, a[2], "writable") : js_mkundef();
  jsval_t enumerable = lkp(js, a[2], "enumerable", 10) ? prop_value(js, a[2], "enumerable") : js_mkundef();
  jsval_t configurable = lkp(js, a[2], "configurable", 12) ? prop_value(js, a[2], "configurable") : js_mkundef();
  jsval_t getter = has_get ? prop_value(js, a[2], "get") : (off ? prop_getter(js, off) : js_mkundef());
  jsval_t setter = has_set ? prop_value(js, a[2], "set") : (off ? prop_setter(js, off) : js_mkundef());

  if (accessor && (has_value || has_writable))
    return js_mkerr(js, "descriptor cannot mix data and accessor fields");
  if (accessor && ((vtype(getter) != T_UNDEF && vtype(getter) != T_FUNC && vtype(getter) != T_CFUNC) ||
                   (vtype(setter) != T_UNDEF && vtype(setter) != T_FUNC && vtype(setter) != T_CFUNC)))
    return js_mkerr(js, "descriptor getter/setter must be functions");

  uint32_t oldflags = off ? prop_flags(js, off) : 0;
  uint32_t flags = 0;
  bool new_writable = has_writable ? js_truthy(js, writable) : (off && (oldflags & V7_PROP_WRITABLE));
  bool new_enumerable = lkp(js, a[2], "enumerable", 10) ? js_truthy(js, enumerable) : (off && (oldflags & V7_PROP_ENUMERABLE));
  bool new_configurable = lkp(js, a[2], "configurable", 12) ? js_truthy(js, configurable) : (off && (oldflags & V7_PROP_CONFIGURABLE));
  if (new_writable && !accessor) flags |= V7_PROP_WRITABLE;
  if (new_enumerable) flags |= V7_PROP_ENUMERABLE;
  if (new_configurable) flags |= V7_PROP_CONFIGURABLE;
  if (accessor) flags |= V7_PROP_ACCESSOR;

  if (off && !(oldflags & V7_PROP_CONFIGURABLE)) {
    if (new_configurable || new_enumerable != !!(oldflags & V7_PROP_ENUMERABLE))
      return js_mkerr(js, "cannot redefine a non-configurable property");
    if (!!(oldflags & V7_PROP_ACCESSOR) != accessor)
      return js_mkerr(js, "cannot change property kind on a non-configurable property");
    if (oldflags & V7_PROP_ACCESSOR) {
      if ((has_get && !v7_same_value(js, getter, prop_getter(js, off))) ||
          (has_set && !v7_same_value(js, setter, prop_setter(js, off))))
        return js_mkerr(js, "cannot change accessor on a non-configurable property");
    } else if (!(oldflags & V7_PROP_WRITABLE)) {
      if (new_writable || (has_value && !v7_same_value(js, value, old_value)))
        return js_mkerr(js, "cannot change a non-writable property");
    }
  }

  if (off) {
    prop_set_flags(js, off, flags);
    if (!accessor && (has_value || !existing_accessor)) saveval(js, off + sizeof(jsoff_t)*2, value);
    else if (accessor) saveval(js, off + sizeof(jsoff_t)*2, js_mkundef());
    prop_set_accessors(js, off, accessor ? getter : js_mkundef(), accessor ? setter : js_mkundef());
  } else {
    jsval_t created = setprop(js, a[0], js_mkstr(js, key, keylen), accessor ? js_mkundef() : value);
    if (is_err(created)) return created;
    off = lkp(js, a[0], key, keylen);
    if (!off) return js_mkerr(js, "failed to create property");
    prop_set_flags(js, off, flags);
    prop_set_accessors(js, off, accessor ? getter : js_mkundef(), accessor ? setter : js_mkundef());
  }
  return a[0];
}
static jsval_t dm_object_set_integrity(struct js *js, jsval_t *a, int n, int freeze) {
  if (!n || vtype(a[0]) != T_OBJ) return n ? a[0] : js_mkerr(js, "Object integrity operation requires an object");
  jsval_t obj = a[0];
  prop_set(js, obj, "__v7_extensible", js_mkfalse());
  jsoff_t off = loadoff(js, (jsoff_t)vdata(obj)) & ~3U;
  while (off && off < js->brk) {
    jsoff_t next = loadoff(js, off) & ~3U; uint32_t flags = prop_flags(js, off);
    flags &= ~V7_PROP_CONFIGURABLE; if (freeze) flags &= ~V7_PROP_WRITABLE;
    prop_set_flags(js, off, flags); off = next;
  }
  return obj;
}
static jsval_t dm_object_seal(struct js *j, jsval_t *a, int n) { return dm_object_set_integrity(j,a,n,0); }
static jsval_t dm_object_freeze(struct js *j, jsval_t *a, int n) { return dm_object_set_integrity(j,a,n,1); }
static jsval_t dm_object_issealed(struct js *js, jsval_t *a, int n) {
  if (!n || vtype(a[0]) != T_OBJ) return js_mktrue();
  if (vtype(prop_value(js,a[0],"__v7_extensible")) == T_BOOL && vdata(prop_value(js,a[0],"__v7_extensible"))) return js_mkfalse();
  jsoff_t off=loadoff(js,(jsoff_t)vdata(a[0]))&~3U; while(off&&off<js->brk){if(prop_flags(js,off)&V7_PROP_CONFIGURABLE)return js_mkfalse();off=loadoff(js,off)&~3U;} return js_mktrue();
}
static jsval_t dm_object_isfrozen(struct js *js, jsval_t *a, int n) {
  jsval_t sealed=dm_object_issealed(js,a,n); if(is_err(sealed)||!vdata(sealed)||!n||vtype(a[0])!=T_OBJ)return sealed;
  jsoff_t off=loadoff(js,(jsoff_t)vdata(a[0]))&~3U; while(off&&off<js->brk){if(prop_flags(js,off)&V7_PROP_WRITABLE)return js_mkfalse();off=loadoff(js,off)&~3U;} return js_mktrue();
}
static jsval_t dm_object_preventextensions(struct js *js, jsval_t *a, int n) { if(!n||vtype(a[0])!=T_OBJ)return n?a[0]:js_mkerr(js,"Object.preventExtensions requires an object");prop_set(js,a[0],"__v7_extensible",js_mkfalse());return a[0]; }
static jsval_t dm_object_isextensible(struct js *js, jsval_t *a, int n) { if(!n||vtype(a[0])!=T_OBJ)return js_mkfalse();jsval_t v=prop_value(js,a[0],"__v7_extensible");return vtype(v)==T_BOOL?mkval(T_BOOL,vdata(v)!=0):js_mktrue(); }


/* JSON: a compact recursive serializer/parser over the VM's native values. */
struct dm_json_out { char *p; size_t len, cap; };
static int dm_json_putn(struct dm_json_out *o, const char *p, size_t n) {
  if (n > SIZE_MAX - o->len - 1) return 0;
  if (o->len + n + 1 > o->cap) { size_t c=o->cap?o->cap:128; while(c<o->len+n+1){if(c>SIZE_MAX/2)return 0;c*=2;} char *q=(char*)realloc(o->p,c);if(!q)return 0;o->p=q;o->cap=c; }
  if(n)memcpy(o->p+o->len,p,n);o->len+=n;o->p[o->len]='\0';return 1;
}
static int dm_json_char(struct dm_json_out *o, char c) { return dm_json_putn(o,&c,1); }
static int dm_json_quote(struct dm_json_out *o, const char *s, size_t n) {
  if(!dm_json_char(o,'"'))return 0;
  for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];char esc[7];
    switch(c){case '"': if(!dm_json_putn(o,"\\\"",2))return 0;break;case '\\':if(!dm_json_putn(o,"\\\\",2))return 0;break;
      case '\b':if(!dm_json_putn(o,"\\b",2))return 0;break;case '\f':if(!dm_json_putn(o,"\\f",2))return 0;break;
      case '\n':if(!dm_json_putn(o,"\\n",2))return 0;break;case '\r':if(!dm_json_putn(o,"\\r",2))return 0;break;case '\t':if(!dm_json_putn(o,"\\t",2))return 0;break;
      default:if(c<0x20){(void)snprintf(esc,sizeof(esc),"\\u%04x",c);if(!dm_json_putn(o,esc,6))return 0;}else if(!dm_json_char(o,(char)c))return 0;}
  }
  return dm_json_char(o,'"');
}
static int dm_json_write_value(struct js *, struct dm_json_out *, jsval_t, jsoff_t *, unsigned);
static int dm_json_write_value(struct js *js, struct dm_json_out *out, jsval_t value, jsoff_t *stack, unsigned depth) {
  if(depth>64)return 0;
  switch(vtype(value)){
    case T_UNDEF: case T_HOLE: case T_FUNC: case T_CFUNC: return -1;
    case T_NULL:return dm_json_putn(out,"null",4);
    case T_BOOL:return dm_json_putn(out,vdata(value)?"true":"false",vdata(value)?4:5);
    case T_NUM:{double d=tod(value);if(!isfinite(d))return dm_json_putn(out,"null",4);char num[64];int n=snprintf(num,sizeof(num),"%.17g",d);return n>0&&dm_json_putn(out,num,(size_t)n);}
    case T_STR:{size_t n=0;const char *p=js_getstr(js,value,&n);return p?dm_json_quote(out,p,n):0;}
    case T_OBJ:break;
    default:return -1;
  }
  if (vtype(prop_value(js, value, "__v7_date")) == T_BOOL && vdata(prop_value(js, value, "__v7_date"))) {
    jsval_t json_value = prop_invoke(js, prop_value(js, value, "toJSON"), value, NULL, 0);
    if (is_err(json_value)) return 0;
    return dm_json_write_value(js, out, json_value, stack, depth + 1);
  }
  for(unsigned i=0;i<depth;i++)if(stack[i]==(jsoff_t)vdata(value))return 0;
  stack[depth]=(jsoff_t)vdata(value);
  if(dm_array(js,value)){
    if(!dm_json_char(out,'['))return 0;unsigned long n=dm_array_length(js,value);
    for(unsigned long i=0;i<n;i++){char key[32];(void)snprintf(key,sizeof(key),"%lu",i);if(i&&!dm_json_char(out,','))return 0;
      if(!lkp(js,value,key,strlen(key))){if(!dm_json_putn(out,"null",4))return 0;continue;}
      jsval_t item=resolveprop(js,mkval(T_PROP,lkp(js,value,key,strlen(key))));int r=dm_json_write_value(js,out,item,stack,depth+1);
      if(r==-1){if(!dm_json_putn(out,"null",4))return 0;}else if(!r)return 0;
    }
    return dm_json_char(out,']');
  }
  if(!dm_json_char(out,'{'))return 0;
  jsoff_t props[2048];unsigned count=0; jsoff_t off=loadoff(js,(jsoff_t)vdata(value))&~3U;
  while(off&&off<js->brk&&count<2048){props[count++]=off;off=loadoff(js,off)&~3U;}
  bool first=true;
  while(count){off=props[--count];jsval_t raw=loadval(js,off+sizeof(jsoff_t)*2);if(vtype(raw)==T_HOLE||!(prop_flags(js,off)&V7_PROP_ENUMERABLE))continue;
    jsoff_t keyoff=loadoff(js,off+sizeof(jsoff_t));size_t kn=offtolen(loadoff(js,keyoff));const char *key=(const char*)&js->mem[keyoff+sizeof(jsoff_t)];
    if((kn>=5&&memcmp(key,"__dm_",5)==0)||(kn>=5&&memcmp(key,"__v7_",5)==0))continue;
    jsval_t actual=resolveprop(js,mkval(T_PROP,off));if(vtype(actual)==T_UNDEF||vtype(actual)==T_FUNC||vtype(actual)==T_CFUNC)continue;
    if(!first&&!dm_json_char(out,','))return 0;first=false;
    if(!dm_json_quote(out,key,kn)||!dm_json_char(out,':'))return 0;
    int r=dm_json_write_value(js,out,actual,stack,depth+1);if(r<=0)return 0;
  }
  return dm_json_char(out,'}');
}
static jsval_t dm_json_stringify(struct js *js, jsval_t *a, int n) {
  if(n<1)return js_mkundef();jsval_t value=resolveprop(js,a[0]);
  if(vtype(value)==T_UNDEF||vtype(value)==T_FUNC||vtype(value)==T_CFUNC)return js_mkundef();
  struct dm_json_out out={0};jsoff_t stack[65]={0};int ok=dm_json_write_value(js,&out,value,stack,0);
  if(!ok){free(out.p);return js_mkerr(js,"JSON.stringify failed (cycle, depth, or memory limit)");}
  if(ok==-1){free(out.p);return js_mkundef();}
  jsval_t result=js_mkstr(js,out.p?out.p:"",out.len);free(out.p);return result;
}
struct dm_json_parser { struct js *js; const char *s; size_t n,i; unsigned depth; };
static void dm_json_ws(struct dm_json_parser *p){while(p->i<p->n&&(p->s[p->i]==' '||p->s[p->i]=='\t'||p->s[p->i]=='\r'||p->s[p->i]=='\n'))p->i++;}
static int dm_json_hex4(struct dm_json_parser *p, unsigned *out){if(p->i+4>p->n)return 0;unsigned v=0;for(int k=0;k<4;k++){int c=(unsigned char)p->s[p->i++];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)return 0;v=(v<<4)|(unsigned)d;}*out=v;return 1;}
static int dm_json_utf8(struct dm_json_out *o,unsigned cp){char b[4];size_t n;if(cp<=0x7f){b[0]=(char)cp;n=1;}else if(cp<=0x7ff){b[0]=(char)(0xc0|(cp>>6));b[1]=(char)(0x80|(cp&63));n=2;}else if(cp<=0xffff){b[0]=(char)(0xe0|(cp>>12));b[1]=(char)(0x80|((cp>>6)&63));b[2]=(char)(0x80|(cp&63));n=3;}else{b[0]=(char)(0xf0|(cp>>18));b[1]=(char)(0x80|((cp>>12)&63));b[2]=(char)(0x80|((cp>>6)&63));b[3]=(char)(0x80|(cp&63));n=4;}return dm_json_putn(o,b,n);}
static jsval_t dm_json_parse_value(struct dm_json_parser *p);
static jsval_t dm_json_parse_string(struct dm_json_parser *p){if(p->i>=p->n||p->s[p->i++]!='"')return js_mkerr(p->js,"JSON string expected");struct dm_json_out out={0};while(p->i<p->n){unsigned char c=(unsigned char)p->s[p->i++];if(c=='"'){jsval_t v=js_mkstr(p->js,out.p?out.p:"",out.len);free(out.p);return v;}if(c<0x20){free(out.p);return js_mkerr(p->js,"invalid control character in JSON string");}if(c=='\\'){if(p->i>=p->n){free(out.p);return js_mkerr(p->js,"invalid JSON escape");}char e=p->s[p->i++];switch(e){case '"':case '\\':case '/':if(!dm_json_char(&out,e))goto oom;break;case 'b':if(!dm_json_char(&out,'\b'))goto oom;break;case 'f':if(!dm_json_char(&out,'\f'))goto oom;break;case 'n':if(!dm_json_char(&out,'\n'))goto oom;break;case 'r':if(!dm_json_char(&out,'\r'))goto oom;break;case 't':if(!dm_json_char(&out,'\t'))goto oom;break;case 'u':{unsigned cp;if(!dm_json_hex4(p,&cp)){free(out.p);return js_mkerr(p->js,"invalid JSON unicode escape");}if(cp>=0xd800&&cp<=0xdbff&&p->i+6<=p->n&&p->s[p->i]=='\\'&&p->s[p->i+1]=='u'){p->i+=2;unsigned low;if(!dm_json_hex4(p,&low)||low<0xdc00||low>0xdfff){free(out.p);return js_mkerr(p->js,"invalid JSON surrogate pair");}cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}if(!dm_json_utf8(&out,cp))goto oom;break;}default:free(out.p);return js_mkerr(p->js,"invalid JSON escape");}}else if(!dm_json_char(&out,(char)c))goto oom;}free(out.p);return js_mkerr(p->js,"unterminated JSON string");oom:free(out.p);return js_mkerr(p->js,"out of memory parsing JSON string");}
static jsval_t dm_json_parse_value(struct dm_json_parser *p){dm_json_ws(p);if(p->depth++>64)return js_mkerr(p->js,"JSON nesting limit exceeded");if(p->i>=p->n)return js_mkerr(p->js,"unexpected end of JSON input");char c=p->s[p->i];jsval_t out=js_mkundef();
  if(c=='"'){out=dm_json_parse_string(p);p->depth--;return out;}
  if(c=='{'){p->i++;out=mkobj(p->js,0);dm_json_ws(p);if(p->i<p->n&&p->s[p->i]=='}'){p->i++;p->depth--;return out;}for(;;){dm_json_ws(p);jsval_t key=dm_json_parse_string(p);if(is_err(key)){p->depth--;return key;}dm_json_ws(p);if(p->i>=p->n||p->s[p->i++]!=':'){p->depth--;return js_mkerr(p->js,"':' expected in JSON object");}jsval_t val=dm_json_parse_value(p);if(is_err(val)){p->depth--;return val;}size_t kn=0;const char *ks=js_getstr(p->js,key,&kn);if(!ks||is_err(setprop(p->js,out,key,val))){p->depth--;return js_mkerr(p->js,"failed to create JSON property");}dm_json_ws(p);if(p->i<p->n&&p->s[p->i]=='}'){p->i++;p->depth--;return out;}if(p->i>=p->n||p->s[p->i++]!=','){p->depth--;return js_mkerr(p->js,"',' expected in JSON object");}}}
  if(c=='['){p->i++;out=dm_array_new(p->js);dm_json_ws(p);if(p->i<p->n&&p->s[p->i]==']'){p->i++;p->depth--;return out;}unsigned long ix=0;for(;;){jsval_t val=dm_json_parse_value(p);if(is_err(val)){p->depth--;return val;}dm_array_store(p->js,out,ix++,val);dm_json_ws(p);if(p->i<p->n&&p->s[p->i]==']'){p->i++;p->depth--;return out;}if(p->i>=p->n||p->s[p->i++]!=','){p->depth--;return js_mkerr(p->js,"',' expected in JSON array");}}}
  if(c=='t'&&p->i+4<=p->n&&!memcmp(p->s+p->i,"true",4)){p->i+=4;out=js_mktrue();}
  else if(c=='f'&&p->i+5<=p->n&&!memcmp(p->s+p->i,"false",5)){p->i+=5;out=js_mkfalse();}
  else if(c=='n'&&p->i+4<=p->n&&!memcmp(p->s+p->i,"null",4)){p->i+=4;out=js_mknull();}
  else {size_t st=p->i;if(p->s[p->i]=='-')p->i++;if(p->i>=p->n)return js_mkerr(p->js,"invalid JSON number");if(p->s[p->i]=='0')p->i++;else if(p->s[p->i]>='1'&&p->s[p->i]<='9'){while(p->i<p->n&&p->s[p->i]>='0'&&p->s[p->i]<='9')p->i++;}else return js_mkerr(p->js,"invalid JSON value");if(p->i<p->n&&p->s[p->i]=='.'){p->i++;size_t d=p->i;while(p->i<p->n&&p->s[p->i]>='0'&&p->s[p->i]<='9')p->i++;if(p->i==d)return js_mkerr(p->js,"invalid JSON fraction");}if(p->i<p->n&&(p->s[p->i]=='e'||p->s[p->i]=='E')){p->i++;if(p->i<p->n&&(p->s[p->i]=='+'||p->s[p->i]=='-'))p->i++;size_t d=p->i;while(p->i<p->n&&p->s[p->i]>='0'&&p->s[p->i]<='9')p->i++;if(p->i==d)return js_mkerr(p->js,"invalid JSON exponent");}char num[128];size_t nn=p->i-st;if(nn>=sizeof(num))return js_mkerr(p->js,"JSON number too long");memcpy(num,p->s+st,nn);num[nn]=0;char *end=NULL;double d=strtod(num,&end);if(!end||*end)return js_mkerr(p->js,"invalid JSON number");out=tov(d);}
  p->depth--;return out;
}
static jsval_t dm_json_parse(struct js *js, jsval_t *a, int n){if(n<1||vtype(a[0])!=T_STR)return js_mkerr(js,"JSON.parse requires a string");size_t len=0;const char *src=js_getstr(js,a[0],&len);struct dm_json_parser p={js,src,len,0,0};jsval_t v=dm_json_parse_value(&p);if(is_err(v))return v;dm_json_ws(&p);if(p.i!=p.n)return js_mkerr(js,"unexpected trailing JSON input");return v;}

/* Date: UTC-based ES5-compatible core time accessors and ISO formatting. */
static double dm_date_now_ms(void){struct timespec ts;if(timespec_get(&ts,TIME_UTC)==TIME_UTC)return (double)ts.tv_sec*1000.0+(double)ts.tv_nsec/1000000.0;return (double)time(NULL)*1000.0;}
static int64_t dm_days_from_civil(int y,unsigned m,unsigned d){y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yoe=(unsigned)(y-era*400);unsigned doy=(153*(m+(m>2?(unsigned)-3:9))+2)/5+d-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;return (int64_t)era*146097+(int64_t)doe-719468;}
static int dm_date_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static double dm_date_parse_ms(const char *s) {
  if (!s) return NAN;
  size_t n = strlen(s), i = 0;
  if (n < 10 || !is_digit((unsigned char)s[0]) || !is_digit((unsigned char)s[1]) ||
      !is_digit((unsigned char)s[2]) || !is_digit((unsigned char)s[3]) ||
      s[4] != '-' || !is_digit((unsigned char)s[5]) || !is_digit((unsigned char)s[6]) ||
      s[7] != '-' || !is_digit((unsigned char)s[8]) || !is_digit((unsigned char)s[9])) return NAN;
  int y=(s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+(s[3]-'0');
  int mo=(s[5]-'0')*10+(s[6]-'0'), d=(s[8]-'0')*10+(s[9]-'0');
  i=10; int h=0,mi=0,se=0,ms=0,offset=0;
  static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (mo<1 || mo>12 || d<1 || d>mdays[mo-1]+(mo==2&&dm_date_leap(y))) return NAN;
  if (i<n) {
    if (s[i]!='T' && s[i]!='t') return NAN;
    i++;
    if (i+8>n || !is_digit((unsigned char)s[i]) || !is_digit((unsigned char)s[i+1]) || s[i+2]!=':' ||
        !is_digit((unsigned char)s[i+3]) || !is_digit((unsigned char)s[i+4]) || s[i+5]!=':' ||
        !is_digit((unsigned char)s[i+6]) || !is_digit((unsigned char)s[i+7])) return NAN;
    h=(s[i]-'0')*10+s[i+1]-'0'; mi=(s[i+3]-'0')*10+s[i+4]-'0'; se=(s[i+6]-'0')*10+s[i+7]-'0'; i+=8;
    if (i<n && s[i]=='.') {
      i++; size_t start=i; int digits=0;
      while(i<n && is_digit((unsigned char)s[i])) { if(digits<3)ms=ms*10+(s[i]-'0'); digits++; i++; }
      if (digits==0 || digits>3) return NAN;
      while(digits++<3) ms*=10;
      if(i==start)return NAN;
    }
    if (i<n && (s[i]=='Z'||s[i]=='z')) i++;
    else if (i<n && (s[i]=='+'||s[i]=='-')) {
      int sign=s[i++]=='+'?1:-1;
      if(i+5>n || !is_digit((unsigned char)s[i]) || !is_digit((unsigned char)s[i+1]) || s[i+2]!=':' ||
         !is_digit((unsigned char)s[i+3]) || !is_digit((unsigned char)s[i+4])) return NAN;
      int oh=(s[i]-'0')*10+s[i+1]-'0', om=(s[i+3]-'0')*10+s[i+4]-'0';
      if(oh>23||om>59)return NAN; offset=sign*(oh*60+om); i+=5;
    }
  }
  if(i!=n || h>23 || mi>59 || se>59) return NAN;
  return (double)dm_days_from_civil(y,(unsigned)mo,(unsigned)d)*86400000.0 +
         h*3600000.0 + mi*60000.0 + se*1000.0 + ms - (double)offset*60000.0;
}
static double dm_date_this_ms(struct js *js){jsval_t v=prop_value(js,js->this_val,"_time");return vtype(v)==T_NUM?tod(v):NAN;}
static int dm_date_tm(struct js *js,struct tm *out,int *millis){double ms=dm_date_this_ms(js);if(!isfinite(ms)||fabs(ms)>8.64e15)return 0;double sec=floor(ms/1000.0);double rem=ms-sec*1000.0;time_t t=(time_t)sec;struct tm *pt=gmtime(&t);if(!pt)return 0;*out=*pt;if(millis)*millis=(int)rem;return 1;}
static jsval_t dm_date_construct(struct js *js,jsval_t *a,int n){
  if(n==0)return tov(dm_date_now_ms());
  if(n==1)return tov(vtype(a[0])==T_STR?dm_date_parse_ms(js_getstr(js,a[0],NULL)):dm_to_number(js,a[0]));
  int y=(int)dm_to_number(js,a[0]), mo=(int)dm_to_number(js,a[1])+1;
  int d=n>2&&vtype(a[2])!=T_UNDEF?(int)dm_to_number(js,a[2]):1;
  int h=n>3&&vtype(a[3])!=T_UNDEF?(int)dm_to_number(js,a[3]):0;
  int mi=n>4&&vtype(a[4])!=T_UNDEF?(int)dm_to_number(js,a[4]):0;
  int se=n>5&&vtype(a[5])!=T_UNDEF?(int)dm_to_number(js,a[5]):0;
  int ms=n>6&&vtype(a[6])!=T_UNDEF?(int)dm_to_number(js,a[6]):0;
  if(y>=0&&y<=99)y+=1900;
  while(mo<=0){mo+=12;y--;} while(mo>12){mo-=12;y++;}
  if(y < -271820 || y > 275760) return tov(NAN);
  double day=(double)dm_days_from_civil(y,(unsigned)mo,1)+(double)d-1.0;
  return tov(day*86400000.0+(double)h*3600000.0+(double)mi*60000.0+(double)se*1000.0+(double)ms);
}
static jsval_t dm_date_now_native(struct js *js,jsval_t *a,int n){(void)js;(void)a;(void)n;return tov(dm_date_now_ms());}
static jsval_t dm_date_gettime(struct js *js,jsval_t *a,int n){(void)a;(void)n;return tov(dm_date_this_ms(js));}
static jsval_t dm_date_valueof(struct js *js,jsval_t *a,int n){return dm_date_gettime(js,a,n);}
static jsval_t dm_date_iso(struct js *js,jsval_t *a,int n){(void)a;(void)n;struct tm t;int milli;if(!dm_date_tm(js,&t,&milli))return js_mkerr(js,"Invalid time value");char b[40];int z=snprintf(b,sizeof(b),"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec,milli);return js_mkstr(js,b,(size_t)z);}
static jsval_t dm_date_tostring(struct js *js,jsval_t *a,int n){jsval_t v=dm_date_iso(js,a,n);if(is_err(v))return js_mkstr(js,"Invalid Date",12);return v;}
static jsval_t dm_date_json(struct js *js,jsval_t *a,int n){if(!isfinite(dm_date_this_ms(js))||fabs(dm_date_this_ms(js))>8.64e15)return js_mknull();return dm_date_tostring(js,a,n);}
static jsval_t dm_date_utcstring(struct js *js,jsval_t *a,int n){(void)a;(void)n;struct tm t;int ms;if(!dm_date_tm(js,&t,&ms))return js_mkstr(js,"Invalid Date",12);static const char *wd[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};static const char *mo[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};char b[80];int z=snprintf(b,sizeof(b),"%s, %02d %s %04d %02d:%02d:%02d GMT",wd[t.tm_wday],t.tm_mday,mo[t.tm_mon],t.tm_year+1900,t.tm_hour,t.tm_min,t.tm_sec);return js_mkstr(js,b,(size_t)z);}
static jsval_t dm_date_settime(struct js *js,jsval_t *a,int n){if(n<1)return js_mkerr(js,"Date.setTime requires a timestamp");jsval_t value=tov(dm_to_number(js,a[0]));prop_set(js,js->this_val,"_time",value);return value;}
static jsval_t dm_date_getfield(struct js *js,jsval_t *a,int n,int field){(void)a;(void)n;struct tm t;int ms;if(!dm_date_tm(js,&t,&ms))return tov(NAN);switch(field){case 0:return tov(t.tm_year+1900);case 1:return tov(t.tm_mon);case 2:return tov(t.tm_mday);case 3:return tov(t.tm_wday);case 4:return tov(t.tm_hour);case 5:return tov(t.tm_min);case 6:return tov(t.tm_sec);case 7:return tov(ms);case 8:return tov(0);default:return tov(NAN);}}
#define DM_DATE_GETTER(name,field) static jsval_t name(struct js*j,jsval_t*a,int n){return dm_date_getfield(j,a,n,field);}
DM_DATE_GETTER(dm_date_year,0) DM_DATE_GETTER(dm_date_month,1) DM_DATE_GETTER(dm_date_day,2) DM_DATE_GETTER(dm_date_weekday,3) DM_DATE_GETTER(dm_date_hours,4) DM_DATE_GETTER(dm_date_minutes,5) DM_DATE_GETTER(dm_date_seconds,6) DM_DATE_GETTER(dm_date_millis,7) DM_DATE_GETTER(dm_date_tzoffset,8)
static jsval_t dm_date_utc(struct js *js,jsval_t *a,int n){(void)js;if(n<2)return js_mkerr(js,"Date.UTC requires year and month");int y=(int)dm_to_number(js,a[0]),mo=(int)dm_to_number(js,a[1])+1;int d=n>2?(int)dm_to_number(js,a[2]):1,h=n>3?(int)dm_to_number(js,a[3]):0,mi=n>4?(int)dm_to_number(js,a[4]):0,se=n>5?(int)dm_to_number(js,a[5]):0,ms=n>6?(int)dm_to_number(js,a[6]):0;if(y>=0&&y<=99)y+=1900;while(mo<=0){mo+=12;y--;}while(mo>12){mo-=12;y++;}return tov((double)dm_days_from_civil(y,(unsigned)mo,(unsigned)d)*86400000.0+h*3600000.0+mi*60000.0+se*1000.0+ms);}
static jsval_t dm_date_parse_native(struct js *js,jsval_t *a,int n){(void)js;if(n<1||vtype(a[0])!=T_STR)return tov(NAN);return tov(dm_date_parse_ms(js_getstr(js,a[0],NULL)));}
static void dm_register_date(struct js *js){const char *src="(value, month, day, hours, minutes, seconds, milliseconds) { this.__v7_date = true; if (value === undefined) this._time = __dm_date_construct(); else if (month === undefined) this._time = __dm_date_construct(value); else this._time = __dm_date_construct(value, month, day, hours, minutes, seconds, milliseconds); }";jsval_t ctor=js_make_function(js,src,strlen(src));jsval_t proto=function_prototype_value(js,ctor);prop_set(js,mkval(T_OBJ,0),"__dm_date_construct",js_mkfun(dm_date_construct));prop_set(js,ctor,"now",js_mkfun(dm_date_now_native));prop_set(js,ctor,"parse",js_mkfun(dm_date_parse_native));prop_set(js,ctor,"UTC",js_mkfun(dm_date_utc));
#define DM_DATE_METHOD(name,fn) prop_set(js,proto,name,js_mkfun(fn))
  DM_DATE_METHOD("getTime",dm_date_gettime);DM_DATE_METHOD("valueOf",dm_date_valueof);DM_DATE_METHOD("toISOString",dm_date_iso);DM_DATE_METHOD("toJSON",dm_date_json);DM_DATE_METHOD("toString",dm_date_tostring);DM_DATE_METHOD("toUTCString",dm_date_utcstring);DM_DATE_METHOD("setTime",dm_date_settime);DM_DATE_METHOD("getFullYear",dm_date_year);DM_DATE_METHOD("getUTCFullYear",dm_date_year);DM_DATE_METHOD("getMonth",dm_date_month);DM_DATE_METHOD("getUTCMonth",dm_date_month);DM_DATE_METHOD("getDate",dm_date_day);DM_DATE_METHOD("getUTCDate",dm_date_day);DM_DATE_METHOD("getDay",dm_date_weekday);DM_DATE_METHOD("getUTCDay",dm_date_weekday);DM_DATE_METHOD("getHours",dm_date_hours);DM_DATE_METHOD("getUTCHours",dm_date_hours);DM_DATE_METHOD("getMinutes",dm_date_minutes);DM_DATE_METHOD("getUTCMinutes",dm_date_minutes);DM_DATE_METHOD("getSeconds",dm_date_seconds);DM_DATE_METHOD("getUTCSeconds",dm_date_seconds);DM_DATE_METHOD("getMilliseconds",dm_date_millis);DM_DATE_METHOD("getUTCMilliseconds",dm_date_millis);DM_DATE_METHOD("getTimezoneOffset",dm_date_tzoffset);
#undef DM_DATE_METHOD
  prop_set(js,mkval(T_OBJ,0),"Date",ctor);}


/* A compact, bounded ECMAScript-style RegExp subset. It supports constructor
 * and /pattern/flags syntax, escapes, character classes, dot, anchors, i/g/m
 * flags and * + ? quantifiers. Grouping, alternation, backreferences and full
 * ES5 RegExp grammar are intentionally not claimed as complete. */
struct dm_regex { const char *p; size_t pn; int ignore_case; int multiline; unsigned budget; };
static int dm_re_fold(int c,int ic){return ic&&c>='A'&&c<='Z'?c+32:c;}
static int dm_re_atom(struct dm_regex *r,size_t *pi,unsigned char ch){size_t i=*pi;if(i>=r->pn)return 0;unsigned char c=(unsigned char)r->p[i++];int ok=0;
 if(c=='\\'){if(i>=r->pn)return 0;unsigned char e=(unsigned char)r->p[i++];switch(e){case 'd':ok=ch>='0'&&ch<='9';break;case 'D':ok=!(ch>='0'&&ch<='9');break;case 'w':ok=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_';break;case 'W':ok=!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_');break;case 's':ok=ch==' '||ch=='\t'||ch=='\r'||ch=='\n'||ch=='\f';break;case 'S':ok=!(ch==' '||ch=='\t'||ch=='\r'||ch=='\n'||ch=='\f');break;case 'n':ok=ch=='\n';break;case 'r':ok=ch=='\r';break;case 't':ok=ch=='\t';break;default:ok=dm_re_fold(ch,r->ignore_case)==dm_re_fold(e,r->ignore_case);break;}}
 else if(c=='.')ok=ch!='\n'&&ch!='\r';
 else if(c=='['){int negate=0,matched=0;if(i<r->pn&&r->p[i]=='^'){negate=1;i++;}while(i<r->pn&&r->p[i]!=']'){unsigned char first=(unsigned char)r->p[i++];if(first=='\\'&&i<r->pn)first=(unsigned char)r->p[i++];if(i+1<r->pn&&r->p[i]=='-'&&r->p[i+1]!=']'){i++;unsigned char last=(unsigned char)r->p[i++];if(dm_re_fold(ch,r->ignore_case)>=dm_re_fold(first,r->ignore_case)&&dm_re_fold(ch,r->ignore_case)<=dm_re_fold(last,r->ignore_case))matched=1;}else if(dm_re_fold(ch,r->ignore_case)==dm_re_fold(first,r->ignore_case))matched=1;}if(i>=r->pn||r->p[i]!=']')return 0;i++;ok=negate?!matched:matched;}
 else ok=dm_re_fold(ch,r->ignore_case)==dm_re_fold(c,r->ignore_case);
 *pi=i;return ok;}
static int dm_re_here(struct dm_regex *r,size_t pi,const char *text,size_t tn,size_t ti,size_t *end,unsigned depth){if(++r->budget>100000||depth>128)return 0;if(pi>=r->pn){*end=ti;return 1;}if(r->p[pi]=='$')return ti==tn||(r->multiline&&ti<tn&&(text[ti]=='\n'||text[ti]=='\r'));if(r->p[pi]=='^'&&pi==0){int line_start=ti==0||(r->multiline&&ti>0&&(text[ti-1]=='\n'||text[ti-1]=='\r'));return line_start?dm_re_here(r,pi+1,text,tn,ti,end,depth+1):0;}
 size_t atom_end=pi;int one=dm_re_atom(r,&atom_end,0);(void)one; /* parse atom once below with the current text byte */
 size_t probe=pi; if(probe<r->pn&&r->p[probe]=='['){probe++;if(probe<r->pn&&r->p[probe]=='^')probe++;while(probe<r->pn&&r->p[probe]!=']'){if(r->p[probe]=='\\'&&probe+1<r->pn)probe++;probe++;}if(probe<r->pn)probe++;}else if(probe<r->pn&&r->p[probe]=='\\')probe+=probe+1<r->pn?2:1;else probe++;
 char quant=probe<r->pn?r->p[probe]:0;int min=quant=='+'?1:0;size_t nextpi=probe+(quant=='*'||quant=='+'||quant=='?'?1:0);size_t positions[2048];unsigned count=0;positions[0]=ti;
 while(count<2047&&ti+count<tn){size_t q=pi;if(!dm_re_atom(r,&q,(unsigned char)text[ti+count]))break;count++;positions[count]=ti+count;if(quant!='*'&&quant!='+' )break;}
 if(quant=='?'){count=count?1:0;}
 if(quant==0){if(!count)return 0;return dm_re_here(r,nextpi,text,tn,ti+1,end,depth+1);}
 if(count<(unsigned)min)return 0;for(int k=(int)count;k>=min;k--)if(dm_re_here(r,nextpi,text,tn,positions[k],end,depth+1))return 1;return 0;}
static int dm_regex_find(const char *pattern,size_t pn,const char *text,size_t tn,int ic,int multiline,size_t from,size_t *start,size_t *end){struct dm_regex r={pattern,pn,ic,multiline,0};int anchored=pn&&pattern[0]=='^';size_t last=(anchored&&!multiline)?0:tn;if(from>tn)return 0;for(size_t i=from;i<=last;i++){r.budget=0;size_t e=0;if(dm_re_here(&r,0,text,tn,i,&e,0)){*start=i;*end=e;return 1;}if(anchored&&!multiline)break;}return 0;}
static int dm_regex_args(struct js *js,jsval_t re,const char **pattern,size_t *pn,int *ic,int *global,int *multiline){if(vtype(re)!=T_OBJ||vtype(prop_value(js,re,"__v7_regex"))!=T_BOOL)return 0;jsval_t p=prop_value(js,re,"source"),f=prop_value(js,re,"flags");*pattern=vtype(p)==T_STR?js_getstr(js,p,pn):"";if(vtype(p)!=T_STR)*pn=0;size_t fn=0;const char *fs=vtype(f)==T_STR?js_getstr(js,f,&fn):"";int seen_i=0,seen_g=0,seen_m=0;for(size_t i=0;i<fn;i++){int *seen=fs[i]=='i'?&seen_i:fs[i]=='g'?&seen_g:fs[i]=='m'?&seen_m:NULL;if(!seen||*seen)return 0;*seen=1;}*ic=seen_i;*global=seen_g;*multiline=seen_m;return 1;}
static jsval_t dm_regex_exec_text(struct js *js,jsval_t re,const char *text,size_t tn,int return_array){const char *pattern;size_t pn,st,en;int ic=0,global=0,multiline=0;if(!dm_regex_args(js,re,&pattern,&pn,&ic,&global,&multiline))return js_mkerr(js,"RegExp method receiver is not a valid RegExp");size_t from=0;if(global){jsval_t li=prop_value(js,re,"lastIndex");if(vtype(li)==T_NUM&&tod(li)>0)from=(size_t)tod(li);}if(!dm_regex_find(pattern,pn,text,tn,ic,multiline,from,&st,&en)){if(global)prop_set(js,re,"lastIndex",tov(0));return return_array?js_mknull():js_mkfalse();}if(global)prop_set(js,re,"lastIndex",tov((double)en));if(!return_array)return js_mktrue();jsval_t out=dm_array_new(js);dm_array_store(js,out,0,js_mkstr(js,text+st,en-st));prop_set(js,out,"index",tov((double)st));prop_set(js,out,"input",js_mkstr(js,text,tn));return out;}
static jsval_t dm_regexp_test(struct js *js,jsval_t *a,int n){
  size_t tn=0; const char *text=""; char converted[256];
  if(n>0){
    if(vtype(a[0])==T_STR) text=js_getstr(js,a[0],&tn);
    else { tn=tostr(js,a[0],converted,sizeof(converted)); if(tn>=sizeof(converted))tn=sizeof(converted)-1; text=converted; }
  }
  return dm_regex_exec_text(js,js->this_val,text,tn,0);
}
static jsval_t dm_regexp_exec(struct js *js,jsval_t *a,int n){
  size_t tn=0; const char *text=""; char converted[256];
  if(n>0){
    if(vtype(a[0])==T_STR) text=js_getstr(js,a[0],&tn);
    else { tn=tostr(js,a[0],converted,sizeof(converted)); if(tn>=sizeof(converted))tn=sizeof(converted)-1; text=converted; }
  }
  return dm_regex_exec_text(js,js->this_val,text,tn,1);
}
static jsval_t dm_regexp_to_string(struct js *js,jsval_t *a,int n){(void)a;(void)n;size_t pn=0,fn=0;jsval_t p=prop_value(js,js->this_val,"source"),f=prop_value(js,js->this_val,"flags");const char *ps=vtype(p)==T_STR?js_getstr(js,p,&pn):"",*fs=vtype(f)==T_STR?js_getstr(js,f,&fn):"";struct dm_json_out b={0};dm_json_char(&b,'/');dm_json_putn(&b,ps,pn);dm_json_char(&b,'/');dm_json_putn(&b,fs,fn);jsval_t v=js_mkstr(js,b.p?b.p:"//",b.len);free(b.p);return v;}
static jsval_t dm_string_match(struct js *js,jsval_t *a,int n){size_t tn=0;const char *text=NULL;if(!dm_string_this(js,&text,&tn))text="";jsval_t re=n?a[0]:js_mkstr(js,"",0);if(vtype(re)==T_STR){jsval_t tmp=mkobj(js,0);prop_set(js,tmp,"__v7_regex",js_mktrue());prop_set(js,tmp,"source",re);prop_set(js,tmp,"flags",js_mkstr(js,"",0));re=tmp;}const char *pattern;size_t pn;int ic=0,g=0,m=0;if(!dm_regex_args(js,re,&pattern,&pn,&ic,&g,&m))return js_mkerr(js,"String.match argument is not a valid RegExp");if(!g)return dm_regex_exec_text(js,re,text,tn,1);jsval_t out=dm_array_new(js);size_t from=0,count=0,st,en;prop_set(js,re,"lastIndex",tov(0));while(from<=tn&&dm_regex_find(pattern,pn,text,tn,ic,m,from,&st,&en)){dm_array_store(js,out,count++,js_mkstr(js,text+st,en-st));from=en>st?en:en+1;}prop_set(js,re,"lastIndex",tov(0));return count?out:js_mknull();}
static jsval_t dm_string_search(struct js *js,jsval_t *a,int n){size_t tn=0;const char *text;if(!dm_string_this(js,&text,&tn))return js_mkerr(js,"String.search receiver is not a string");jsval_t re=n?a[0]:js_mkstr(js,"",0);if(vtype(re)==T_STR){jsval_t tmp=mkobj(js,0);prop_set(js,tmp,"__v7_regex",js_mktrue());prop_set(js,tmp,"source",re);prop_set(js,tmp,"flags",js_mkstr(js,"",0));re=tmp;}const char *pattern;size_t pn,st,en;int ic=0,g=0,m=0;if(!dm_regex_args(js,re,&pattern,&pn,&ic,&g,&m))return tov(-1);return dm_regex_find(pattern,pn,text,tn,ic,m,0,&st,&en)?tov((double)st):tov(-1);}
static jsval_t dm_regexp_literal(struct js *js,const char *token,size_t token_len){if(token_len<2||token[0]!='/' )return js_mkerr(js,"invalid RegExp literal");size_t close=token_len-1;while(close>0&&token[close]!='/')close--;if(close==0)return js_mkerr(js,"unterminated RegExp literal");size_t pn=close-1,fn=token_len-close-1;jsval_t ctor=prop_value(js,mkval(T_OBJ,0),"RegExp");jsval_t proto=function_prototype_value(js,ctor);jsval_t re=mkobj(js,(jsoff_t)vdata(proto));prop_set(js,re,"__v7_regex",js_mktrue());prop_set(js,re,"source",js_mkstr(js,token+1,pn));prop_set(js,re,"flags",js_mkstr(js,token+close+1,fn));prop_set(js,re,"lastIndex",tov(0));return re;}
static void dm_register_regexp(struct js *js){const char *src="(pattern, flags) { this.__v7_regex = true; this.source = pattern === undefined ? '' : pattern; this.flags = flags === undefined ? '' : flags; this.lastIndex = 0; }";jsval_t ctor=js_make_function(js,src,strlen(src));jsval_t proto=function_prototype_value(js,ctor);prop_set(js,proto,"test",js_mkfun(dm_regexp_test));prop_set(js,proto,"exec",js_mkfun(dm_regexp_exec));prop_set(js,proto,"toString",js_mkfun(dm_regexp_to_string));prop_set(js,mkval(T_OBJ,0),"RegExp",ctor);}

static jsval_t dm_math_abs(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.abs requires a number"); return tov(fabs(tod(a[0]))); }
static jsval_t dm_math_floor(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.floor requires a number"); return tov(floor(tod(a[0]))); }
static jsval_t dm_math_ceil(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.ceil requires a number"); return tov(ceil(tod(a[0]))); }
static jsval_t dm_math_round(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.round requires a number"); double x=tod(a[0]); if(x<0.0&&x>=-0.5)return tov(-0.0); return tov(floor(x + 0.5)); }
static jsval_t dm_math_trunc(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.trunc requires a number"); return tov(trunc(tod(a[0]))); }
static jsval_t dm_math_sqrt(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.sqrt requires a number"); return tov(sqrt(tod(a[0]))); }
static jsval_t dm_math_pow(struct js *js, jsval_t *a, int n) { if (n < 2 || vtype(a[0]) != T_NUM || vtype(a[1]) != T_NUM) return js_mkerr(js, "Math.pow requires two numbers"); return tov(pow(tod(a[0]), tod(a[1]))); }
static jsval_t dm_math_sin(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.sin requires a number"); return tov(sin(tod(a[0]))); }
static jsval_t dm_math_cos(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.cos requires a number"); return tov(cos(tod(a[0]))); }
static jsval_t dm_math_log(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.log requires a number"); return tov(log(tod(a[0]))); }
static jsval_t dm_math_exp(struct js *js, jsval_t *a, int n) { if (n < 1 || vtype(a[0]) != T_NUM) return js_mkerr(js, "Math.exp requires a number"); return tov(exp(tod(a[0]))); }
static jsval_t dm_math_tan(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.tan requires a number");return tov(tan(tod(a[0]))); }
static jsval_t dm_math_atan(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.atan requires a number");return tov(atan(tod(a[0]))); }
static jsval_t dm_math_atan2(struct js *js, jsval_t *a, int n) { if(n<2||vtype(a[0])!=T_NUM||vtype(a[1])!=T_NUM)return js_mkerr(js,"Math.atan2 requires two numbers");return tov(atan2(tod(a[0]),tod(a[1]))); }
static jsval_t dm_math_asin(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.asin requires a number");return tov(asin(tod(a[0]))); }
static jsval_t dm_math_acos(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.acos requires a number");return tov(acos(tod(a[0]))); }
static jsval_t dm_math_log10(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.log10 requires a number");return tov(log10(tod(a[0]))); }
static jsval_t dm_math_log2(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.log2 requires a number");return tov(log2(tod(a[0]))); }
static jsval_t dm_math_cbrt(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.cbrt requires a number");return tov(cbrt(tod(a[0]))); }
static jsval_t dm_math_fround(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.fround requires a number");return tov((double)(float)tod(a[0])); }
static jsval_t dm_math_max(struct js *js, jsval_t *a, int n) { double v = -INFINITY; for (int i=0;i<n;i++) { if(vtype(a[i])!=T_NUM)return js_mkerr(js,"Math.max requires numbers"); if(isnan(tod(a[i])))return tov(NAN); if(tod(a[i])>v)v=tod(a[i]); } return tov(v); }
static jsval_t dm_math_min(struct js *js, jsval_t *a, int n) { double v = INFINITY; for (int i=0;i<n;i++) { if(vtype(a[i])!=T_NUM)return js_mkerr(js,"Math.min requires numbers"); if(isnan(tod(a[i])))return tov(NAN); if(tod(a[i])<v)v=tod(a[i]); } return tov(v); }
static int v7_random_seeded;
static jsval_t dm_math_random(struct js *js, jsval_t *a, int n) { (void)js;(void)a;(void)n; if(!v7_random_seeded){srand((unsigned)time(NULL));v7_random_seeded=1;} return tov((double)rand() / ((double)RAND_MAX + 1.0)); }
static jsval_t dm_math_sign(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.sign requires a number"); double x=tod(a[0]); return tov(x>0?1:x<0?-1:x); }
static jsval_t dm_math_imul(struct js *js, jsval_t *a, int n) { if(n<2||vtype(a[0])!=T_NUM||vtype(a[1])!=T_NUM)return js_mkerr(js,"Math.imul requires two numbers"); return tov(js_to_int32_number(js_to_uint32(tod(a[0]))*js_to_uint32(tod(a[1])))); }
static jsval_t dm_math_clz32(struct js *js, jsval_t *a, int n) { if(n<1||vtype(a[0])!=T_NUM)return js_mkerr(js,"Math.clz32 requires a number"); uint32_t x=js_to_uint32(tod(a[0])); int bits=0; if(x==0)return tov(32); while((x&0x80000000U)==0){bits++;x<<=1U;} return tov(bits); }
static jsval_t dm_math_hypot(struct js *js, jsval_t *a, int n) { double result=0; for(int i=0;i<n;i++){if(vtype(a[i])!=T_NUM)return js_mkerr(js,"Math.hypot requires numbers");result=hypot(result,tod(a[i]));}return tov(result); }
static jsval_t dm_math_expm1(struct js *js, jsval_t *a, int n) { return tov(expm1(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_log1p(struct js *js, jsval_t *a, int n) { return tov(log1p(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_sinh(struct js *js, jsval_t *a, int n) { return tov(sinh(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_cosh(struct js *js, jsval_t *a, int n) { return tov(cosh(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_tanh(struct js *js, jsval_t *a, int n) { return tov(tanh(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_asinh(struct js *js, jsval_t *a, int n) { return tov(asinh(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_acosh(struct js *js, jsval_t *a, int n) { return tov(acosh(n ? dm_to_number(js, a[0]) : NAN)); }
static jsval_t dm_math_atanh(struct js *js, jsval_t *a, int n) { return tov(atanh(n ? dm_to_number(js, a[0]) : NAN)); }

static double dm_to_number(struct js *js, jsval_t v) {
  if (vtype(v) == T_NUM) return tod(v);
  if (vtype(v) == T_NULL) return 0.0;
  if (vtype(v) == T_BOOL) return vdata(v) ? 1.0 : 0.0;
  if (vtype(v) == T_UNDEF) return NAN;
  if (vtype(v) == T_STR) { size_t n=0; const char *s=js_getstr(js,v,&n); char *end=NULL; double d=strtod(s,&end); if(end==s){while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r')s++;return *s?NAN:0.0;} while(*end==' '||*end=='\t'||*end=='\n'||*end=='\r')end++; return *end?NAN:d; }
  return NAN;
}
static jsval_t dm_global_isnan(struct js *js, jsval_t *a, int n) { return mkval(T_BOOL, isnan(n?dm_to_number(js,a[0]):NAN)); }
static jsval_t dm_global_isfinite(struct js *js, jsval_t *a, int n) { return mkval(T_BOOL, isfinite(n?dm_to_number(js,a[0]):NAN)); }
static jsval_t dm_global_parseint(struct js *js, jsval_t *a, int n) {
  if(n<1)return tov(NAN);if(vtype(a[0])==T_NUM)return tov(trunc(tod(a[0])));if(vtype(a[0])!=T_STR)return tov(NAN);
  const char *s=js_getstr(js,a[0],NULL);int base=n>1&&vtype(a[1])==T_NUM?(int)tod(a[1]):0;if(base!=0&&(base<2||base>36))return tov(NAN);
  if(base==0){const char *p=s;while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r')p++;base=(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))?16:10;}
  char *end=NULL;long value=strtol(s,&end,base);return end==s?tov(NAN):tov((double)value);
}
static jsval_t dm_global_parsefloat(struct js *js, jsval_t *a, int n) {
  if(n<1)return tov(NAN);if(vtype(a[0])==T_NUM)return a[0];if(vtype(a[0])!=T_STR)return tov(NAN);const char *s=js_getstr(js,a[0],NULL);char *end=NULL;double value=strtod(s,&end);return end==s?tov(NAN):tov(value);
}

static jsval_t dm_number_native(struct js *js, jsval_t *a, int n) {
  return tov(n > 0 ? dm_to_number(js, a[0]) : 0.0);
}
static jsval_t dm_number_isnan(struct js *js, jsval_t *a, int n) {
  (void) js;
  return mkval(T_BOOL, n > 0 && vtype(a[0]) == T_NUM && isnan(tod(a[0])));
}
static jsval_t dm_number_isfinite(struct js *js, jsval_t *a, int n) {
  (void) js;
  return mkval(T_BOOL, n > 0 && vtype(a[0]) == T_NUM && isfinite(tod(a[0])));
}
static jsval_t dm_number_isinteger(struct js *js, jsval_t *a, int n) {
  (void) js;
  if (n < 1 || vtype(a[0]) != T_NUM) return js_mkfalse();
  double d = tod(a[0]);
  return mkval(T_BOOL, isfinite(d) && trunc(d) == d);
}
static jsval_t dm_number_issafeinteger(struct js *js, jsval_t *a, int n) {
  (void) js;
  if (n < 1 || vtype(a[0]) != T_NUM) return js_mkfalse();
  double d = tod(a[0]);
  return mkval(T_BOOL, isfinite(d) && trunc(d) == d &&
                        d >= -9007199254740991.0 &&
                        d <= 9007199254740991.0);
}

static void dm_register_collection(struct js *js, const char *name, const char *ctor_src, int setmode, int weak) {
  jsval_t ctor=js_make_function(js,ctor_src,strlen(ctor_src));if(is_err(ctor))return;
  jsval_t proto=function_prototype_value(js,ctor);
  prop_set(js,proto,setmode?"add":"set",js_mkfun(dm_collection_set));
  if (setmode) {
    if (!weak) {
      prop_set(js,proto,"values",js_mkfun(dm_collection_values));
      prop_set(js,proto,"keys",js_mkfun(dm_collection_values));
      prop_set(js,proto,"entries",js_mkfun(dm_collection_entries));
    }
  } else {
    prop_set(js,proto,"get",js_mkfun(dm_collection_get));
    if (!weak) {
      prop_set(js,proto,"keys",js_mkfun(dm_collection_keys));
      prop_set(js,proto,"values",js_mkfun(dm_collection_values));
      prop_set(js,proto,"entries",js_mkfun(dm_collection_entries));
    }
  }
  prop_set(js,proto,"has",js_mkfun(dm_collection_has));
  prop_set(js,proto,"delete",js_mkfun(dm_collection_delete));
  if(!weak)prop_set(js,proto,"clear",js_mkfun(dm_collection_clear));
  (void)setprop(js,mkval(T_OBJ,0),js_mkstr(js,name,strlen(name)),ctor);
}

static const char *v7_mode_name(void);
static jsval_t v7_set_mode_native(struct js *, jsval_t *, int);
static jsval_t v7_get_mode_native(struct js *, jsval_t *, int);
static jsval_t v7_stats_native(struct js *, jsval_t *, int);
static jsval_t v7_line_lock_native(struct js *, jsval_t *, int);
static jsval_t v7_set_decat_native(struct js *, jsval_t *, int);
static jsval_t v7_clear_cache_native(struct js *, jsval_t *, int);
static jsval_t v7_prepare_native(struct js *, jsval_t *, int);
static jsval_t v7_is_prepared_native(struct js *, jsval_t *, int);

struct js *js_create(void *buf, size_t len) {
  struct js *js = NULL;
  if (len < sizeof(*js) + esize(T_OBJ)) return js;
  memset(buf, 0, len);                       // Important!
  js = (struct js *) buf;                    // struct js lives at the beginning
  js->mem = (uint8_t *) (js + 1);            // Then goes memory for JS data
  js->size = (jsoff_t) (len - sizeof(*js));  // JS memory size
  js->scope = mkobj(js, 0);                  // Create global scope
  prop_set(js, js->scope, "__dm_var_scope", js_mktrue());
  js->this_val = js->scope;
  {
    const char *ctor_src = "(executor) { let p=Promise.__dm_pending(); let resolve=function(v){Promise.__dm_resolve(p,v);}; let reject=function(v){Promise.__dm_reject(p,v);}; try { executor(resolve,reject); } catch(e) { reject(e); } return p; }";
    jsval_t promise = js_make_function(js, ctor_src, strlen(ctor_src));
    jsval_t proto = function_prototype_value(js, promise);
    js->promise_proto = (jsoff_t) vdata(proto);
    prop_set(js, promise, "resolve", js_mkfun(promise_resolve_native));
    prop_set(js, promise, "reject", js_mkfun(promise_reject_native));
    prop_set(js, promise, "__dm_pending", js_mkfun(promise_pending_native));
    prop_set(js, promise, "__dm_resolve", js_mkfun(promise_resolve_into_native));
    prop_set(js, promise, "__dm_reject", js_mkfun(promise_reject_into_native));
    prop_set(js, proto, "then", js_mkfun(promise_then_native));
    prop_set(js, proto, "catch", js_mkfun(promise_catch_native));
    prop_set(js, proto, "finally", js_mkfun(promise_finally_native));
    prop_set(js, mkval(T_OBJ, 0), "Promise", promise);
  }
  {
    jsval_t global = mkval(T_OBJ, 0);
    jsval_t console_obj = mkobj(js, 0);
    jsval_t v7_api = mkobj(js, 0);
    prop_set(js, v7_api, "setMode", js_mkfun(v7_set_mode_native));
    prop_set(js, v7_api, "getMode", js_mkfun(v7_get_mode_native));
    prop_set(js, v7_api, "stats", js_mkfun(v7_stats_native));
    prop_set(js, v7_api, "lineLock", js_mkfun(v7_line_lock_native));
    prop_set(js, v7_api, "setDecat", js_mkfun(v7_set_decat_native));
    prop_set(js, v7_api, "clearCache", js_mkfun(v7_clear_cache_native));
    prop_set(js, v7_api, "prepare", js_mkfun(v7_prepare_native));
    prop_set(js, v7_api, "isPrepared", js_mkfun(v7_is_prepared_native));
    prop_set(js, v7_api, "mode", js_mkstr(js, v7_mode_name(), strlen(v7_mode_name())));
    prop_set(js, global, "V7", v7_api);
    prop_set(js, global, "globalThis", global);
    prop_set(js, global, "NaN", tov(NAN));
    prop_set(js, global, "Infinity", tov(INFINITY));
    prop_set(js, global, "isNaN", js_mkfun(dm_global_isnan));
    prop_set(js, global, "isFinite", js_mkfun(dm_global_isfinite));
    prop_set(js, global, "parseInt", js_mkfun(dm_global_parseint));
    prop_set(js, global, "parseFloat", js_mkfun(dm_global_parsefloat));
    { const char *number_src = "(value = 0) { return __dm_number(value); }";
      prop_set(js, global, "__dm_number", js_mkfun(dm_number_native));
      jsval_t number_api = js_make_function(js, number_src, strlen(number_src));
      prop_set(js, number_api, "isNaN", js_mkfun(dm_number_isnan));
      prop_set(js, number_api, "isFinite", js_mkfun(dm_number_isfinite));
      prop_set(js, number_api, "isInteger", js_mkfun(dm_number_isinteger));
      prop_set(js, number_api, "isSafeInteger", js_mkfun(dm_number_issafeinteger));
      prop_set(js, number_api, "parseInt", js_mkfun(dm_global_parseint));
      prop_set(js, number_api, "parseFloat", js_mkfun(dm_global_parsefloat));
      prop_set(js, number_api, "NaN", tov(NAN));
      prop_set(js, number_api, "POSITIVE_INFINITY", tov(INFINITY));
      prop_set(js, number_api, "NEGATIVE_INFINITY", tov(-INFINITY));
      prop_set(js, number_api, "MAX_SAFE_INTEGER", tov(9007199254740991.0));
      prop_set(js, number_api, "MIN_SAFE_INTEGER", tov(-9007199254740991.0));
      prop_set(js, number_api, "EPSILON", tov(2.2204460492503130808472633361816e-16));
      prop_set(js, global, "Number", number_api);
    }

    prop_set(js, global, "queueMicrotask", js_mkfun(queue_microtask_native));
    prop_set(js, global, "setTimeout", js_mkfun(set_timeout_native));
    prop_set(js, global, "clearTimeout", js_mkfun(clear_timer_native));
    prop_set(js, global, "setInterval", js_mkfun(set_interval_native));
    prop_set(js, global, "clearInterval", js_mkfun(clear_timer_native));
    prop_set(js, global, "readFile", js_mkfun(read_file_native));
    prop_set(js, global, "writeFile", js_mkfun(write_file_native));
    prop_set(js, global, "delay", js_mkfun(delay_native));
    prop_set(js, global, "require", js_mkfun(require_native));
    prop_set(js, global, "__dm_modules", mkobj(js, 0));
    prop_set(js, global, "print", js_mkfun(console_log_native));
    { jsval_t math_api = mkobj(js, 0);
      prop_set(js, math_api, "abs", js_mkfun(dm_math_abs)); prop_set(js, math_api, "floor", js_mkfun(dm_math_floor));
      prop_set(js, math_api, "ceil", js_mkfun(dm_math_ceil)); prop_set(js, math_api, "round", js_mkfun(dm_math_round));
      prop_set(js, math_api, "trunc", js_mkfun(dm_math_trunc)); prop_set(js, math_api, "sqrt", js_mkfun(dm_math_sqrt));
      prop_set(js, math_api, "pow", js_mkfun(dm_math_pow)); prop_set(js, math_api, "sin", js_mkfun(dm_math_sin));
      prop_set(js, math_api, "cos", js_mkfun(dm_math_cos)); prop_set(js, math_api, "log", js_mkfun(dm_math_log));
      prop_set(js, math_api, "tan", js_mkfun(dm_math_tan)); prop_set(js, math_api, "atan", js_mkfun(dm_math_atan));
      prop_set(js, math_api, "atan2", js_mkfun(dm_math_atan2)); prop_set(js, math_api, "asin", js_mkfun(dm_math_asin));
      prop_set(js, math_api, "acos", js_mkfun(dm_math_acos)); prop_set(js, math_api, "log10", js_mkfun(dm_math_log10));
      prop_set(js, math_api, "log2", js_mkfun(dm_math_log2)); prop_set(js, math_api, "cbrt", js_mkfun(dm_math_cbrt));
      prop_set(js, math_api, "fround", js_mkfun(dm_math_fround));
      prop_set(js, math_api, "exp", js_mkfun(dm_math_exp)); prop_set(js, math_api, "max", js_mkfun(dm_math_max));
      prop_set(js, math_api, "min", js_mkfun(dm_math_min)); prop_set(js, math_api, "random", js_mkfun(dm_math_random));
      prop_set(js, math_api, "sign", js_mkfun(dm_math_sign)); prop_set(js, math_api, "imul", js_mkfun(dm_math_imul));
      prop_set(js, math_api, "clz32", js_mkfun(dm_math_clz32)); prop_set(js, math_api, "hypot", js_mkfun(dm_math_hypot));
      prop_set(js, math_api, "expm1", js_mkfun(dm_math_expm1)); prop_set(js, math_api, "log1p", js_mkfun(dm_math_log1p));
      prop_set(js, math_api, "sinh", js_mkfun(dm_math_sinh)); prop_set(js, math_api, "cosh", js_mkfun(dm_math_cosh));
      prop_set(js, math_api, "tanh", js_mkfun(dm_math_tanh)); prop_set(js, math_api, "asinh", js_mkfun(dm_math_asinh));
      prop_set(js, math_api, "acosh", js_mkfun(dm_math_acosh)); prop_set(js, math_api, "atanh", js_mkfun(dm_math_atanh));
      prop_set(js, math_api, "PI", tov(3.14159265358979323846)); prop_set(js, math_api, "E", tov(2.71828182845904523536));
      prop_set(js, math_api, "LN2", tov(0.69314718055994530942)); prop_set(js, math_api, "SQRT2", tov(1.41421356237309504880));
      prop_set(js, math_api, "LN10", tov(2.30258509299404568402)); prop_set(js, math_api, "LOG2E", tov(1.44269504088896340736));
      prop_set(js, math_api, "LOG10E", tov(0.43429448190325182765)); prop_set(js, math_api, "SQRT1_2", tov(0.70710678118654752440));
      prop_set(js, global, "Math", math_api);
    }
    { jsval_t object_api = mkobj(js, 0);
      prop_set(js, object_api, "keys", js_mkfun(dm_object_keys));
      prop_set(js, object_api, "values", js_mkfun(dm_object_values));
      prop_set(js, object_api, "entries", js_mkfun(dm_object_entries));
      prop_set(js, object_api, "assign", js_mkfun(dm_object_assign));
      prop_set(js, object_api, "hasOwn", js_mkfun(dm_object_hasown));
      prop_set(js, object_api, "is", js_mkfun(dm_object_is));
      prop_set(js, object_api, "getOwnPropertyNames", js_mkfun(dm_object_names));
      prop_set(js, object_api, "create", js_mkfun(dm_object_create));
      prop_set(js, object_api, "fromEntries", js_mkfun(dm_object_fromentries));
      prop_set(js, object_api, "groupBy", js_mkfun(dm_object_groupby));
      prop_set(js, object_api, "defineProperty", js_mkfun(dm_object_defineproperty));
      prop_set(js, object_api, "getOwnPropertyDescriptor", js_mkfun(dm_object_descriptor));
      prop_set(js, object_api, "freeze", js_mkfun(dm_object_freeze));
      prop_set(js, object_api, "seal", js_mkfun(dm_object_seal));
      prop_set(js, object_api, "preventExtensions", js_mkfun(dm_object_preventextensions));
      prop_set(js, object_api, "isFrozen", js_mkfun(dm_object_isfrozen));
      prop_set(js, object_api, "isSealed", js_mkfun(dm_object_issealed));
      prop_set(js, object_api, "isExtensible", js_mkfun(dm_object_isextensible));
      prop_set(js, global, "Object", object_api);
    }
    { jsval_t json_api = mkobj(js, 0);
      prop_set(js, json_api, "parse", js_mkfun(dm_json_parse));
      prop_set(js, json_api, "stringify", js_mkfun(dm_json_stringify));
      prop_set(js, global, "JSON", json_api);
    }
    dm_register_date(js);
    dm_register_regexp(js);
    { jsval_t array_api = mkobj(js, 0);
      prop_set(js, array_api, "isArray", js_mkfun(dm_array_isarray));
      prop_set(js, array_api, "of", js_mkfun(dm_array_of));
      prop_set(js, array_api, "from", js_mkfun(dm_array_from));
      prop_set(js, global, "Array", array_api);
    }
    dm_register_collection(js,"Map","() { this.__dm_keys=[]; this.__dm_values=[]; this.__dm_setmode=false; this.__dm_weak=false; this.size=0; }",0,0);
    dm_register_collection(js,"Set","() { this.__dm_keys=[]; this.__dm_values=[]; this.__dm_setmode=true; this.__dm_weak=false; this.size=0; }",1,0);
    dm_register_collection(js,"WeakMap","() { this.__dm_keys=[]; this.__dm_values=[]; this.__dm_setmode=false; this.__dm_weak=true; this.size=0; }",0,1);
    dm_register_collection(js,"WeakSet","() { this.__dm_keys=[]; this.__dm_values=[]; this.__dm_setmode=true; this.__dm_weak=true; this.size=0; }",1,1);
    prop_set(js, global, "Symbol", js_mkfun(dm_symbol_native));
    prop_set(js, console_obj, "log", js_mkfun(console_log_native));
    prop_set(js, console_obj, "warn", js_mkfun(console_log_native));
    prop_set(js, console_obj, "error", js_mkfun(console_error_native));
    prop_set(js, console_obj, "time", js_mkfun(console_time_native));
    prop_set(js, console_obj, "timeEnd", js_mkfun(console_time_end_native));
    prop_set(js, console_obj, "table", js_mkfun(console_table_native));
    prop_set(js, console_obj, "trace", js_mkfun(console_trace_native));
    prop_set(js, global, "console", console_obj);
  }                  // Global this in script mode
  js->size = js->size / 8U * 8U;             // Align js->size by 8 byte
  js->lwm = js->size;                        // Initial LWM: 100% free
  js->gct = js->size / 2;
  return js;
}

// clang-format off
void js_setgct(struct js *js, size_t gct) { js->gct = (jsoff_t) gct; }
void js_setmaxcss(struct js *js, size_t max) { js->maxcss = (jsoff_t) max; }
jsval_t js_mktrue(void) { return mkval(T_BOOL, 1); }
jsval_t js_mkfalse(void) { return mkval(T_BOOL, 0); }
jsval_t js_mkundef(void) { return mkval(T_UNDEF, 0); }
jsval_t js_mknull(void) { return mkval(T_NULL, 0); }
jsval_t js_mknum(double value) { return tov(value); }
jsval_t js_mkobj(struct js *js) { return mkobj(js, 0); }
jsval_t js_mkfun(jsval_t (*fn)(struct js *, jsval_t *, int)) { return mkval(T_CFUNC, (size_t) (void *) fn); }
double js_getnum(jsval_t value) { return tod(value); }
int js_getbool(jsval_t value) { return vdata(value) & 1 ? 1 : 0; }

jsval_t js_glob(struct js *js) { (void) js; return mkval(T_OBJ, 0); }

void js_set(struct js *js, jsval_t obj, const char *key, jsval_t val) {
  if (vtype(obj) == T_OBJ) setprop(js, obj, js_mkstr(js, key, strlen(key)), val);
}

char *js_getstr(struct js *js, jsval_t value, size_t *len) {
  if (vtype(value) != T_STR) return NULL;
  jsoff_t n, off = vstr(js, value, &n);
  if (len != NULL) *len = n;
  return (char *) &js->mem[off];
}

int js_type(jsval_t val) {
  switch (vtype(val)) {  
    case T_UNDEF:   return JS_UNDEF;
    case T_NULL:    return JS_NULL;
    case T_BOOL:    return vdata(val) == 0 ? JS_FALSE: JS_TRUE;
    case T_STR:     return JS_STR;
    case T_NUM:     return JS_NUM;
    case T_ERR:     return JS_ERR;
    default:        return JS_PRIV;
  }
}
void js_stats(struct js *js, size_t *total, size_t *lwm, size_t *css) {
  if (total) *total = js->size;
  if (lwm) *lwm = js->lwm;
  if (css) *css = js->css;
}
// clang-format on

bool js_chkargs(jsval_t *args, int nargs, const char *spec) {
  int i = 0, ok = 1;
  for (; ok && i < nargs && spec[i]; i++) {
    uint8_t t = vtype(args[i]), c = (uint8_t) spec[i];
    ok = (c == 'b' && t == T_BOOL) || (c == 'd' && t == T_NUM) ||
         (c == 's' && t == T_STR) || (c == 'j');
  }
  if (spec[i] != '\0' || i != nargs) ok = 0;
  return ok;
}

/* A small source-level template expander. It deliberately only rewrites
 * untagged templates; tagged templates require the full template-object
 * semantics and are rejected/documented separately. This keeps the parser and
 * execution backend unchanged. */
static int dm_append(char **out, size_t *used, size_t *cap, const char *p, size_t n) {
  if (*used + n + 1 > *cap) {
    size_t nc = *cap ? *cap : 128;
    while (nc < *used + n + 1) { if (nc > SIZE_MAX / 2) return 0; nc *= 2; }
    char *q = (char *)realloc(*out, nc); if (!q) return 0; *out = q; *cap = nc;
  }
  memcpy(*out + *used, p, n); *used += n; (*out)[*used] = 0; return 1;
}
static int dm_append_quoted(char **out, size_t *used, size_t *cap, const char *p, size_t n) {
  if (!dm_append(out, used, cap, "\"", 1)) return 0;
  for (size_t i = 0; i < n; i++) {
    char c = p[i], esc[2] = {'\\', c};
    if (c == '\\' || c == '"') { if (!dm_append(out, used, cap, esc, 2)) return 0; }
    else if (c == '\n') { if (!dm_append(out, used, cap, "\\n", 2)) return 0; }
    else if (c == '\r') { if (!dm_append(out, used, cap, "\\r", 2)) return 0; }
    else if (c == '\t') { if (!dm_append(out, used, cap, "\\t", 2)) return 0; }
    else if (!dm_append(out, used, cap, &c, 1)) return 0;
  }
  return dm_append(out, used, cap, "\"", 1);
}
static char *dm_expand_templates(const char *src, size_t len, size_t *outlen) {
  char *out = NULL; size_t used = 0, cap = 0; char quote = 0; int line_comment = 0, block_comment = 0;
  for (size_t i = 0; i < len;) {
    char c = src[i];
    if (line_comment) { if (!dm_append(&out,&used,&cap,src+i,1)) goto fail; i++; if (c=='\n') line_comment=0; continue; }
    if (block_comment) { if (!dm_append(&out,&used,&cap,src+i,1)) goto fail; if (c=='*' && i+1<len && src[i+1]=='/') { if (!dm_append(&out,&used,&cap,src+i+1,1)) goto fail; i+=2; block_comment=0; } else i++; continue; }
    if (quote) { if (!dm_append(&out,&used,&cap,src+i,1)) goto fail; if (c=='\\' && i+1<len) { if (!dm_append(&out,&used,&cap,src+i+1,1)) goto fail; i+=2; continue; } if (c==quote) quote=0; i++; continue; }
    if (c=='/' && i+1<len && src[i+1]=='/') { if (!dm_append(&out,&used,&cap,src+i,2)) goto fail; i+=2; line_comment=1; continue; }
    if (c=='/' && i+1<len && src[i+1]=='*') { if (!dm_append(&out,&used,&cap,src+i,2)) goto fail; i+=2; block_comment=1; continue; }
    if (c=='\'' || c=='"') { quote=c; if (!dm_append(&out,&used,&cap,src+i++,1)) goto fail; continue; }
    if (c!='`') { if (!dm_append(&out,&used,&cap,src+i++,1)) goto fail; continue; }
    /* Template: emit a concatenation expression, balancing nested braces and
     * skipping quoted strings inside each ${...} expression. */
    i++; if (!dm_append(&out,&used,&cap,"(",1)) goto fail;
    int any = 0; size_t lit = i;
    for (;;) {
      size_t j = i;
      while (j < len && src[j] != '`' && !(src[j]=='$' && j+1<len && src[j+1]=='{')) {
        if (src[j]=='\\' && j+1<len) j+=2; else j++;
      }
      if (any && !dm_append(&out,&used,&cap," + ",3)) goto fail;
      if (!dm_append_quoted(&out,&used,&cap,src+lit,j-lit)) goto fail;
      any = 1; i = j;
      if (i >= len) goto fail;
      if (src[i]=='`') { i++; break; }
      i += 2; size_t expr = i; int depth = 1; char q = 0;
      while (i < len && depth) {
        char x = src[i];
        if (q) { if (x=='\\' && i+1<len) { i+=2; continue; } if (x==q) q=0; i++; continue; }
        if (x=='\'' || x=='"' || x=='`') { q=x; i++; continue; }
        if (x=='{') depth++; else if (x=='}') depth--;
        if (depth) i++;
      }
      if (depth || i < expr) goto fail;
      if (!dm_append(&out,&used,&cap," + (",4) || !dm_append(&out,&used,&cap,src+expr,i-expr) || !dm_append(&out,&used,&cap,")",1)) goto fail;
      i++; lit = i;
      /* Continue by adding next literal; it will be joined after this expr. */
      if (i < len && src[i] != '`' && !(src[i]=='$' && i+1<len && src[i+1]=='{')) {
        if (!dm_append(&out,&used,&cap," + ",3)) goto fail;
        size_t k=i; while (k<len && src[k]!='`' && !(src[k]=='$'&&k+1<len&&src[k+1]=='{')) { if(src[k]=='\\'&&k+1<len)k+=2;else k++; }
        if (!dm_append_quoted(&out,&used,&cap,src+i,k-i)) goto fail; i=k; lit=i;
      }
    }
    if (!any) goto fail;
    if (!dm_append(&out,&used,&cap,")",1)) goto fail;
  }
  if (!out) { out=(char*)malloc(1); if(!out)return NULL; out[0]=0; }
  *outlen=used; return out;
fail: free(out); return NULL;
}

/* V7: a conservative hot cache for template-literal source expansion.
 * It caches only a pure preprocessing result, never evaluated values or
 * mutable VM state. Entries are exact-source matched and bounded to 1 MiB. */
#ifndef V7_CACHE_BYTES
#define V7_CACHE_BYTES (1024U * 1024U)
#endif
#define V7_CACHE_SLOTS 32
static struct {
  char *source, *expanded;
  size_t source_len, expanded_len, bytes;
  uint64_t age;
} v7_cache[V7_CACHE_SLOTS];
static size_t v7_cache_bytes;
static uint64_t v7_clock, v7_hits, v7_misses, v7_evictions, v7_evaluations;
static unsigned v7_mode = 1; /* 0 Safe, 1 Turbo, 2 Extreme */

/* Extreme-mode editor preparation cache. This stores complete lexical units,
 * never executes code while the editor is changing, and is independently
 * bounded to 1 MiB. The normal parser still validates and executes on submit. */
#define V7_PREP_SLOTS 32
static struct {
  char *source;
  size_t length, bytes;
  uint64_t age;
} v7_prep[V7_PREP_SLOTS];
static size_t v7_prep_bytes;
static uint64_t v7_prep_hits, v7_prep_misses, v7_prep_rejects, v7_preparse_runs, v7_preparse_errors, v7_liftoff_runs;
static uint64_t v7_turbofan_liftoff_hits, v7_turbofan_liftoff_misses;

/* Return 1 when strings/comments/delimiters are lexically closed. This is an
 * editor completeness check, not a replacement for the JavaScript parser. */
static int v7_source_complete(const char *s, size_t n) {
  char stack[256]; size_t depth=0;
  int quote=0, line_comment=0, block_comment=0, escape=0;
  for (size_t i=0;i<n;i++) {
    unsigned char c=(unsigned char)s[i], lookahead=(i+1<n)?(unsigned char)s[i+1]:0;
    if (line_comment) { if(c=='\n'||c=='\r')line_comment=0; continue; }
    if (block_comment) { if(c=='*'&&lookahead=='/') {block_comment=0;i++;} continue; }
    if (quote) {
      if(escape){escape=0;continue;}
      if(c=='\\'){escape=1;continue;}
      if(c==(unsigned char)quote)quote=0;
      continue;
    }
    if(c=='/'&&lookahead=='/'){line_comment=1;i++;continue;}
    if(c=='/'&&lookahead=='*'){block_comment=1;i++;continue;}
    if(c=='\''||c=='"'||c=='`'){quote=c;continue;}
    if(c=='('||c=='['||c=='{') {
      if(depth>=sizeof(stack))return 0;
      stack[depth++]=(char)c;
    } else if(c==')'||c==']'||c=='}') {
      if(!depth)return 0;
      char open=stack[--depth];
      if(!((open=='('&&c==')')||(open=='['&&c==']')||(open=='{'&&c=='}')))return 0;
    }
  }
  return !quote&&!block_comment&&!depth;
}
static void v7_prep_clear(void) {
  for(unsigned i=0;i<V7_PREP_SLOTS;i++){free(v7_prep[i].source);memset(&v7_prep[i],0,sizeof(v7_prep[i]));}
  v7_prep_bytes=0;
}
static char *v7_prep_get(const char *source,size_t length,size_t *outlen) {
  for(unsigned i=0;i<V7_PREP_SLOTS;i++)if(v7_prep[i].source&&v7_prep[i].length==length&&memcmp(v7_prep[i].source,source,length)==0){
    char *copy=(char*)malloc(length+1U);if(!copy)return NULL;
    memcpy(copy,v7_prep[i].source,length);copy[length]=0;
    v7_prep[i].age=++v7_clock;v7_prep_hits++;if(outlen)*outlen=length;return copy;
  }
  v7_prep_misses++;return NULL;
}
static int v7_prep_put(const char *source,size_t length) {
  if(v7_mode!=2||!length||length+1U>V7_CACHE_BYTES){v7_prep_rejects++;return 0;}
  for(unsigned i=0;i<V7_PREP_SLOTS;i++)if(v7_prep[i].source&&v7_prep[i].length==length&&memcmp(v7_prep[i].source,source,length)==0){v7_prep[i].age=++v7_clock;return 1;}
  unsigned slot=V7_PREP_SLOTS;
  for(unsigned i=0;i<V7_PREP_SLOTS;i++)if(!v7_prep[i].source){slot=i;break;}
  if(slot==V7_PREP_SLOTS){slot=0;for(unsigned i=1;i<V7_PREP_SLOTS;i++)if(v7_prep[i].age<v7_prep[slot].age)slot=i;}
  while(v7_prep_bytes+length+1U>V7_CACHE_BYTES){
    unsigned victim=V7_PREP_SLOTS;
    for(unsigned i=0;i<V7_PREP_SLOTS;i++)if(v7_prep[i].source&&(victim==V7_PREP_SLOTS||v7_prep[i].age<v7_prep[victim].age))victim=i;
    if(victim==V7_PREP_SLOTS)return 0;
    v7_prep_bytes-=v7_prep[victim].bytes;free(v7_prep[victim].source);memset(&v7_prep[victim],0,sizeof(v7_prep[victim]));
  }
  char *copy=(char*)malloc(length+1U);if(!copy)return 0;
  memcpy(copy,source,length);copy[length]=0;
  if(v7_prep[slot].source){v7_prep_bytes-=v7_prep[slot].bytes;free(v7_prep[slot].source);}
  v7_prep[slot].source=copy;v7_prep[slot].length=length;v7_prep[slot].bytes=length+1U;v7_prep[slot].age=++v7_clock;v7_prep_bytes+=length+1U;
  return 1;
}

/* Find the last top-level lexical boundary in an unfinished editor buffer. */
static size_t v7_source_safe_prefix(const char *s,size_t n){
  char stack[256];size_t depth=0,safe=0;
  int quote=0,line_comment=0,block_comment=0,escape=0;
  for(size_t i=0;i<n;i++){
    unsigned char c=(unsigned char)s[i],next=(i+1<n)?(unsigned char)s[i+1]:0;
    if(line_comment){if(c==10||c==13){line_comment=0;if(depth==0)safe=i+1;}continue;}
    if(block_comment){if(c=='*'&&next=='/'){block_comment=0;i++;}continue;}
    if(quote){if(escape){escape=0;continue;}if(c==92){escape=1;continue;}if(c==(unsigned char)quote)quote=0;continue;}
    if(c=='/'&&next=='/'){line_comment=1;i++;continue;}
    if(c=='/'&&next=='*'){block_comment=1;i++;continue;}
    if(c==39||c==34||c==96){quote=c;continue;}
    if(c=='('||c=='['||c=='{'){if(depth<sizeof(stack))stack[depth++]=(char)c;else return safe;continue;}
    if(c==')'||c==']'||c=='}'){
      if(!depth)continue;
      char open=stack[--depth];
      if((open=='('&&c!=')')||(open=='['&&c!=']')||(open=='{'&&c!='}'))return safe;
      if(depth==0)safe=i+1;
      continue;
    }
    if(depth==0&&(c==';'||c==10))safe=i+1;
  }
  return safe;
}

/* Extreme Prep: parser-only validation. This stage never executes user code. */
static int v7_preparse_source(struct js *js,const char *source,size_t length){
  jsval_t scratch=js_mkundef();
  if(!js||!source||!length||length>(size_t)((jsoff_t)-1))return 0;
  size_t parse_length=v7_source_complete(source,length)?length:v7_source_safe_prefix(source,length);
  if(!parse_length)return 1;
  const char *saved_code=js->code;jsoff_t saved_clen=js->clen,saved_pos=js->pos;
  uint8_t saved_tok=js->tok,saved_consumed=js->consumed,saved_flags=js->flags;
  jsval_t saved_tval=js->tval,*saved_cstk=js->cstk;int ok=1;
  js->code=source;js->clen=(jsoff_t)parse_length;js->pos=0;js->tok=TOK_ERR;js->consumed=1;
  js->flags=(uint8_t)(saved_flags|F_NOEXEC);js->tval=js_mkundef();js->cstk=&scratch;v7_preparse_runs++;
  while(v7_next_token(js)!=TOK_EOF&&!is_err(scratch))scratch=js_stmt(js);
  if(is_err(scratch)){
    v7_preparse_errors++;
    if(parse_length==length){
      size_t prefix=v7_source_safe_prefix(source,length);
      if(prefix>0&&prefix<length){
        js->code=saved_code;js->clen=saved_clen;js->pos=saved_pos;js->tok=saved_tok;
        js->consumed=saved_consumed;js->flags=saved_flags;js->tval=saved_tval;js->cstk=saved_cstk;
        return v7_preparse_source(js,source,prefix);
      }
    }
  }
  js->code=saved_code;js->clen=saved_clen;js->pos=saved_pos;js->tok=saved_tok;
  js->consumed=saved_consumed;js->flags=saved_flags;js->tval=saved_tval;js->cstk=saved_cstk;
  return ok;
}
static void v7_cache_clear(void) {
  for (unsigned i=0;i<V7_CACHE_SLOTS;i++) {
    free(v7_cache[i].source); free(v7_cache[i].expanded);
    memset(&v7_cache[i],0,sizeof(v7_cache[i]));
  }
  v7_cache_bytes=0;
}
static char *v7_cache_get(const char *source, size_t length, size_t *outlen) {
  if (v7_mode == 0) return NULL;
  for (unsigned i=0;i<V7_CACHE_SLOTS;i++) {
    if (v7_cache[i].source && v7_cache[i].source_len==length &&
        memcmp(v7_cache[i].source,source,length)==0) {
      char *copy=(char*)malloc(v7_cache[i].expanded_len+1U);
      if (!copy) return NULL;
      memcpy(copy,v7_cache[i].expanded,v7_cache[i].expanded_len+1U);
      v7_cache[i].age=++v7_clock; v7_hits++; *outlen=v7_cache[i].expanded_len;
      return copy;
    }
  }
  v7_misses++; return NULL;
}
static void v7_cache_put(const char *source,size_t slen,const char *expanded,size_t elen) {
  if(v7_mode==0 || slen+elen+2U>V7_CACHE_BYTES)return;
  unsigned slot=V7_CACHE_SLOTS;
  for(unsigned i=0;i<V7_CACHE_SLOTS;i++)if(!v7_cache[i].source){slot=i;break;}
  if(slot==V7_CACHE_SLOTS){
    slot=0;for(unsigned i=1;i<V7_CACHE_SLOTS;i++)if(v7_cache[i].age<v7_cache[slot].age)slot=i;
    v7_cache_bytes-=v7_cache[slot].bytes;free(v7_cache[slot].source);free(v7_cache[slot].expanded);
    memset(&v7_cache[slot],0,sizeof(v7_cache[slot]));v7_evictions++;
  }
  while(v7_cache_bytes+slen+elen+2U>V7_CACHE_BYTES){
    unsigned victim=V7_CACHE_SLOTS;
    for(unsigned i=0;i<V7_CACHE_SLOTS;i++)if(v7_cache[i].source && (victim==V7_CACHE_SLOTS||v7_cache[i].age<v7_cache[victim].age))victim=i;
    if(victim==V7_CACHE_SLOTS)break;
    v7_cache_bytes-=v7_cache[victim].bytes;free(v7_cache[victim].source);free(v7_cache[victim].expanded);
    memset(&v7_cache[victim],0,sizeof(v7_cache[victim]));v7_evictions++;
  }
  char *sc=(char*)malloc(slen+1U),*ec=(char*)malloc(elen+1U);
  if(!sc||!ec){free(sc);free(ec);return;}
  memcpy(sc,source,slen);sc[slen]=0;memcpy(ec,expanded,elen);ec[elen]=0;
  v7_cache[slot].source=sc;v7_cache[slot].expanded=ec;v7_cache[slot].source_len=slen;
  v7_cache[slot].expanded_len=elen;v7_cache[slot].bytes=slen+elen+2U;v7_cache[slot].age=++v7_clock;
  v7_cache_bytes+=v7_cache[slot].bytes;
}
static const char *v7_mode_name(void) { return v7_mode==0?"Safe":v7_mode==1?"Turbo":"Extreme"; }
static jsval_t v7_set_mode_native(struct js *js, jsval_t *args, int nargs) {
  if(nargs<1 || vtype(args[0])!=T_STR)return js_mkerr(js,"V7.setMode expects Safe, Turbo, or Extreme");
  size_t n=0;const char *v=js_getstr(js,args[0],&n);unsigned mode;
  if(n==4&&!memcmp(v,"Safe",4))mode=0;
  else if(n==5&&!memcmp(v,"Turbo",5))mode=1;
  else if(n==7&&!memcmp(v,"Extreme",7))mode=2;
  else return js_mkerr(js,"V7 mode must be Safe, Turbo, or Extreme");
  if(mode!=v7_mode && mode==0){v7_cache_clear();v7_prep_clear();}
  v7_mode=mode;
  prop_set(js,mkval(T_OBJ,0),"__v7_api_mode",js_mkstr(js,v7_mode_name(),strlen(v7_mode_name())));
  jsval_t api=prop_value(js,mkval(T_OBJ,0),"V7");
  if(vtype(api)==T_OBJ)prop_set(js,api,"mode",js_mkstr(js,v7_mode_name(),strlen(v7_mode_name())));
  return js_mkstr(js,v7_mode_name(),strlen(v7_mode_name()));
}
static jsval_t v7_get_mode_native(struct js *js, jsval_t *args, int nargs) {
  (void)args;(void)nargs;return js_mkstr(js,v7_mode_name(),strlen(v7_mode_name()));
}
static jsval_t v7_prepare_native(struct js *js, jsval_t *args, int nargs) {
  if(nargs<1||vtype(args[0])!=T_STR)return js_mkerr(js,"V7.prepare expects a source string");
  size_t n=0;const char *source=js_getstr(js,args[0],&n);
  if(v7_mode!=2||!v7_preparse_source(js,source,n)){v7_prep_rejects++;return js_mkfalse();}
  return v7_prep_put(source,n)?js_mktrue():js_mkfalse();
}
static jsval_t v7_is_prepared_native(struct js *js, jsval_t *args, int nargs) {
  if(nargs<1||vtype(args[0])!=T_STR)return js_mkerr(js,"V7.isPrepared expects a source string");
  size_t n=0;const char *source=js_getstr(js,args[0],&n);
  /* Do not count a read-only lookup as a cache miss. */
  for(unsigned i=0;i<V7_PREP_SLOTS;i++)if(v7_prep[i].source&&v7_prep[i].length==n&&memcmp(v7_prep[i].source,source,n)==0){v7_prep[i].age=++v7_clock;return js_mktrue();}
  return js_mkfalse();
}
static jsval_t v7_stats_native(struct js *js, jsval_t *args, int nargs) {
  (void)args;(void)nargs;jsval_t o=mkobj(js,0);
  prop_set(js,o,"mode",js_mkstr(js,v7_mode_name(),strlen(v7_mode_name())));
  prop_set(js,o,"cacheCapacityBytes",tov((double)V7_CACHE_BYTES));
  prop_set(js,o,"cacheUsedBytes",tov((double)v7_cache_bytes));
  prop_set(js,o,"cacheHits",tov((double)v7_hits));prop_set(js,o,"cacheMisses",tov((double)v7_misses));
  prop_set(js,o,"cacheEvictions",tov((double)v7_evictions));prop_set(js,o,"evaluations",tov((double)v7_evaluations));
  prop_set(js,o,"prepCapacityBytes",tov((double)V7_CACHE_BYTES));
  prop_set(js,o,"prepUsedBytes",tov((double)v7_prep_bytes));
  prop_set(js,o,"prepHits",tov((double)v7_prep_hits));prop_set(js,o,"prepMisses",tov((double)v7_prep_misses));
  prop_set(js,o,"prepRejects",tov((double)v7_prep_rejects));
  prop_set(js,o,"preparseRuns",tov((double)v7_preparse_runs));
  prop_set(js,o,"preparseErrors",tov((double)v7_preparse_errors));
  prop_set(js,o,"liftoffRuns",tov((double)v7_liftoff_runs));
  prop_set(js,o,"turboFanLiftoffHits",tov((double)v7_turbofan_liftoff_hits));
  prop_set(js,o,"turboFanLiftoffMisses",tov((double)v7_turbofan_liftoff_misses));
  prop_set(js,o,"turboFanEnabled",(v7_mode==2?js_mktrue():js_mkfalse()));
  {const char *optimizer=v7_mode==2?"TurboFan-style Prep/Liftoff; V7 interpreter backend":"disabled";prop_set(js,o,"optimizer",js_mkstr(js,optimizer,strlen(optimizer)));}
  prop_set(js,o,"prepEnabled",(v7_mode==2?js_mktrue():js_mkfalse()));
  prop_set(js,o,"nitrousPasses",tov((double)js->nitrous_passes));
  prop_set(js,o,"wastegateVents",tov((double)js->wastegate_vents));
  prop_set(js,o,"lineLockedEntities",tov((double)js->line_locked_entities));
  prop_set(js,o,"kineticSleepMs",tov((double)js->kinetic_sleep_ms));
  prop_set(js,o,"decatEnabled",js->decat?js_mktrue():js_mkfalse());
  return o;
}
static jsval_t v7_line_lock_native(struct js *js, jsval_t *args, int nargs) {
  if(nargs<1||(vtype(args[0])!=T_OBJ&&vtype(args[0])!=T_FUNC))return js_mkerr(js,"V7.lineLock expects an object or function");
  /* Sliding compaction means stable native offsets require freezing GC VM-wide. */
  if(js->nogc!=(jsoff_t)~0){js->nogc=(jsoff_t)~0;js->line_locked_entities=1;}
  return args[0];
}
static jsval_t v7_set_decat_native(struct js *js, jsval_t *args, int nargs) {
  if(!nargs)return js_mkerr(js,"V7.setDecat expects a boolean");
  js->decat=(uint8_t)js_truthy(js,args[0]);if(js->decat)js->strict_mode=0;
  return js->decat?js_mktrue():js_mkfalse();
}
static jsval_t v7_clear_cache_native(struct js *js, jsval_t *args, int nargs) {
  (void) js;
  (void)args;(void)nargs;v7_cache_clear();v7_prep_clear();return js_mktrue();
}


static size_t dm_skip_ws_comments(const char *s, size_t n, size_t i) {
  for (;;) {
    while (i<n && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n'||s[i]=='\f')) i++;
    if (i+1<n && s[i]=='/' && s[i+1]=='/') { i+=2; while(i<n&&s[i]!='\n')i++; continue; }
    if (i+1<n && s[i]=='/' && s[i+1]=='*') { i+=2; while(i+1<n&&!(s[i]=='*'&&s[i+1]=='/'))i++; if(i+1<n)i+=2; continue; }
    return i;
  }
}
static bool dm_has_use_strict(const char *s, size_t n) {
  size_t i = 0;
  for (;;) {
    while (i < n) {
      if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\f') { i++; continue; }
      if (i + 1 < n && s[i] == '/' && s[i+1] == '/') { i += 2; while (i < n && s[i] != '\n') i++; continue; }
      if (i + 1 < n && s[i] == '/' && s[i+1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i+1] == '/')) i++; if (i + 1 < n) i += 2; continue; }
      break;
    }
    if (i >= n || (s[i] != '\'' && s[i] != '"')) return false;
    char quote = s[i++]; size_t begin = i; bool escaped = false;
    while (i < n && s[i] != quote) {
      if (s[i] == '\\') { escaped = true; if (i + 1 < n) i++; }
      i++;
    }
    if (i >= n) return false;
    size_t length = i - begin; bool strict = !escaped && length == 10 && memcmp(s + begin, "use strict", 10) == 0;
    i++;
    bool newline = false;
    for (;;) {
      if (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\f')) { i++; continue; }
      if (i < n && (s[i] == '\r' || s[i] == '\n')) { newline = true; i++; continue; }
      if (i + 1 < n && s[i] == '/' && s[i+1] == '/') { i += 2; while (i < n && s[i] != '\n') i++; newline = true; continue; }
      if (i + 1 < n && s[i] == '/' && s[i+1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i+1] == '/')) { if (s[i] == '\n' || s[i] == '\r') newline = true; i++; } if (i + 1 < n) i += 2; continue; }
      break;
    }
    bool terminated = false;
    if (i < n && s[i] == ';') { i++; terminated = true; }
    else if (newline || i >= n || s[i] == '}') terminated = true;
    if (!terminated) return false;
    if (strict) return true;
    if (i >= n || s[i] == '}') return false;
  }
}

static size_t dm_find_function_end(const char *s, size_t n, size_t start) {
  size_t i=start; unsigned par=0,brace=0; char quote=0;
  while(i<n && s[i]!='(')i++; if(i==n)return 0;
  for(;i<n;i++) { char c=s[i]; if(quote){if(c=='\\'&&i+1<n){i++;continue;}if(c==quote)quote=0;continue;}
    if(i+1<n&&c=='/'&&s[i+1]=='/'){i+=2;while(i<n&&s[i]!='\n')i++;continue;}
    if(i+1<n&&c=='/'&&s[i+1]=='*'){i+=2;while(i+1<n&&!(s[i]=='*'&&s[i+1]=='/'))i++;if(i+1<n)i++;continue;}
    if(c=='\''||c=='\"'||c=='`'){quote=c;continue;} if(c=='(')par++;else if(c==')'&&par)par--;else if(c=='{'&&par==0){brace=1; i++; break;}}
  if(!brace)return 0;
  for(;i<n;i++){char c=s[i];if(quote){if(c=='\\'&&i+1<n){i++;continue;}if(c==quote)quote=0;continue;}
    if(i+1<n&&c=='/'&&s[i+1]=='/'){i+=2;while(i<n&&s[i]!='\n')i++;continue;}
    if(i+1<n&&c=='/'&&s[i+1]=='*'){i+=2;while(i+1<n&&!(s[i]=='*'&&s[i+1]=='/'))i++;if(i+1<n)i++;continue;}
    if(c=='\''||c=='\"'||c=='`'){quote=c;continue;}if(c=='{')brace++;else if(c=='}'&&--brace==0)return i+1;}
  return 0;
}
static void dm_hoist_declarations(struct js *js, const char *source, size_t length) {
  const char *saved=js->code; jsoff_t clen=js->clen,pos=js->pos,toff=js->toff,tlen=js->tlen;
  uint8_t tok=js->tok,consumed=js->consumed,flags=js->flags; jsval_t scope=js->scope;
  jsval_t target=dm_var_scope(js); js->code=source; js->clen=(jsoff_t)length; js->pos=0; js->tok=TOK_ERR; js->consumed=1; js->flags=(uint8_t)(flags|F_NOEXEC);
  while(v7_next_token(js)!=TOK_EOF) {    if(js->tok==TOK_VAR) {
      js->consumed=1; if(v7_next_token(js)==TOK_IDENTIFIER) {
        const char *name=js->code+js->toff; size_t nn=js->tlen;
        if(!lkp(js,target,name,nn)) (void)setprop(js,target,js_mkstr(js,name,nn),js_mkundef());
      }
    } else if(js->tok==TOK_FUNC) {
      js->consumed=1; if(v7_next_token(js)==TOK_IDENTIFIER) {
        const char *name=js->code+js->toff; size_t nn=js->tlen; size_t open=dm_skip_ws_comments(source,length,(size_t)js->pos);
        size_t end=dm_find_function_end(source,length,open);
        if(end>open) {
          jsval_t fn=js_make_function(js,source+open,end-open);
          if(!is_err(fn)) { if(lkp(js,target,name,nn)) { jsoff_t old=lkp(js,target,name,nn); saveval(js,old+sizeof(jsoff_t)*2,fn); } else (void)setprop(js,target,js_mkstr(js,name,nn),fn); }
          js->pos=(jsoff_t)end; js->consumed=1; js->tok=TOK_ERR;
        }
      }
    }
    js->consumed=1;
  }
  js->code=saved;js->clen=clen;js->pos=pos;js->toff=toff;js->tlen=tlen;js->tok=tok;js->consumed=consumed;js->flags=flags;js->scope=scope;
}

jsval_t js_eval(struct js *js, const char *buf, size_t len) {
  // printf("EVAL: [%.*s]\n", (int) len, buf);
  jsval_t res = js_mkundef();
  if (len == (size_t) ~0U) len = strlen(buf);
  uint8_t previous_strict = js->strict_mode;
  if (!js->decat && dm_has_use_strict(buf, len)) js->strict_mode = 1;
  char *expanded = NULL;
  const char *original_buf = buf;
  js->eval_depth++; v7_evaluations++;
  char *prepared_source=NULL;size_t prepared_length=0;
  if(v7_mode==2&&len){
    prepared_source=v7_prep_get(buf,len,&prepared_length);
    if(prepared_source){v7_turbofan_liftoff_hits++;}
    else{
      v7_turbofan_liftoff_misses++;
      if(v7_preparse_source(js,buf,len)&&v7_prep_put(buf,len))prepared_source=v7_prep_get(buf,len,&prepared_length);
    }
    if(prepared_source){buf=prepared_source;len=prepared_length;}
    v7_liftoff_runs++;
  }
  if (memchr(buf, '`', len) != NULL) {
    size_t expanded_len = 0;
    expanded = v7_cache_get(buf,len,&expanded_len);
    if (expanded == NULL) {
      expanded = dm_expand_templates(buf, len, &expanded_len);
      if (expanded != NULL) v7_cache_put(buf,len,expanded,expanded_len);
    }
    if (expanded != NULL) { buf = expanded; len = expanded_len; }
  }
  dm_hoist_declarations(js, buf, len);
  js->consumed = 1;
  js->tok = TOK_ERR;
  js->code = buf;
  js->clen = (jsoff_t) len;
  js->pos = 0;
  js->cstk = &res;
  while (v7_next_token(js) != TOK_EOF && !is_err(res)) {
    res = js_stmt(js);
  }
  if (js->eval_depth) js->eval_depth--;
  if (js->eval_depth == 0 && !js->draining_jobs) {
    drain_microtasks(js);
#ifndef __EMSCRIPTEN__
    run_event_loop(js, 1);
#endif
  }
  js->code = original_buf;
  if (expanded != NULL) free(expanded);
  if (prepared_source != NULL) free(prepared_source);
  js->strict_mode = previous_strict;
  return res;
}

#ifdef JS_DUMP
void js_dump(struct js *js) {
  jsoff_t off = 0, v;
  printf("JS size %u, brk %u, lwm %u, css %u, nogc %u\n", js->size, js->brk,
         js->lwm, (unsigned) js->css, js->nogc);
  while (off < js->brk) {
    memcpy(&v, &js->mem[off], sizeof(v));
    printf(" %5u: ", off);
    if ((v & 3U) == T_OBJ) {
      printf("OBJ %u %u\n", v & ~3U,
             loadoff(js, (jsoff_t) (off + sizeof(off))));
    } else if ((v & 3U) == T_PROP) {
      jsoff_t koff = loadoff(js, (jsoff_t) (off + sizeof(v)));
      jsval_t val = loadval(js, (jsoff_t) (off + sizeof(v) + sizeof(v)));
      printf("PROP next %u, koff %u vtype %d vdata %lu\n", v & ~3U, koff,
             vtype(val), (unsigned long) vdata(val));
    } else if ((v & 3) == T_STR) {
      jsoff_t len = offtolen(v);
      printf("STR %u [%.*s]\n", len, (int) len, js->mem + off + sizeof(v));
    } else {
      printf("???\n");
      break;
    }
    off += esize(v);
  }
}
#endif

/* Portable C embedding API. V7_Init never allocates the VM arena. */
V7 *V7_Init(void *memory, size_t memory_size) {
  if (memory == NULL || memory_size > UINT32_MAX) return NULL;
  return js_create(memory, memory_size);
}

/* V7_Create allocates one contiguous VM arena; release it with V7_Destroy. */
V7 *V7_Create(size_t memory_size) {
  void *memory; V7 *vm;
  if (memory_size > UINT32_MAX || memory_size < sizeof(struct js) + esize(T_OBJ)) return NULL;
  memory = malloc(memory_size); if (memory == NULL) return NULL;
  vm = V7_Init(memory, memory_size); if (vm == NULL) free(memory);
  return vm;
}
void V7_Destroy(V7 *vm) { free(vm); }

jsval_t V7_Eval(V7 *vm, const char *source, size_t length) {
  if (vm == NULL) return js_mkundef();
  if (source == NULL) return length == 0 ? js_eval(vm, "", 0) : js_mkerr(vm, "V7_Eval received null source");
  return js_eval(vm, source, length);
}
jsval_t V7_EvalCString(V7 *vm, const char *source) {
  return source == NULL ? V7_Eval(vm, "", 0) : V7_Eval(vm, source, strlen(source));
}
const char *V7_ToString(V7 *vm, jsval_t value) { return vm == NULL ? "" : js_str(vm, value); }
size_t V7_CopyString(V7 *vm, jsval_t value, char *output, size_t output_size) {
  const char *source = V7_ToString(vm, value); size_t length = strlen(source);
  if (output != NULL && output_size > 0) { size_t n = length < output_size - 1U ? length : output_size - 1U; memcpy(output, source, n); output[n] = '\0'; }
  return length;
}
int V7_GetType(jsval_t value) { return js_type(value); }
double V7_GetNumber(jsval_t value) { return js_getnum(value); }
int V7_IsTruthy(V7 *vm, jsval_t value) { return vm != NULL && js_truthy(vm, value); }
void V7_SetGlobal(V7 *vm, const char *name, jsval_t value) { if (vm != NULL && name != NULL) js_set(vm, js_glob(vm), name, value); }
/* V7 host interface and standalone entry point. */
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define V7_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define V7_KEEPALIVE
#endif

static union {
  max_align_t align;
  unsigned char bytes[1024U * 1024U];
} g_vm_storage;

static struct js *g_vm;
static int g_last_status;

V7_KEEPALIVE int v7_init(void) {
  if (g_vm != NULL) return 0;
  g_vm = js_create(g_vm_storage.bytes, sizeof(g_vm_storage.bytes));
  if (g_vm == NULL) {
    fputs("V7: could not initialize JavaScript VM\n", stderr);
    return 1;
  }
  return 0;
}

/*
 * Evaluate one JavaScript source string and return a malloc-allocated UTF-8
 * result. The caller must free the returned pointer with free(). In Emscripten,
 * JavaScript can call _v7_eval() and then _free() the returned pointer.
 */
V7_KEEPALIVE char *v7_eval(const char *source) {
  jsval_t value;
  const char *rendered;
  char *copy;
  size_t length;

  if (source == NULL) source = "";
  if (v7_init() != 0) {
    g_last_status = 1;
    return NULL;
  }

  value = js_eval(g_vm, source, strlen(source));
  g_last_status = (js_type(value) == JS_ERR) ? 1 : 0;
  rendered = js_str(g_vm, value);  if (rendered == NULL) rendered = "";
  length = strlen(rendered);
  copy = (char *) malloc(length + 1U);
  if (copy == NULL) {
    g_last_status = 1;
    return NULL;
  }
  memcpy(copy, rendered, length + 1U);
  return copy;
}

V7_KEEPALIVE int v7_last_status(void) {
  return g_last_status;
}
/* Browser hosts can call this after returning to their own event loop. */
V7_KEEPALIVE int v7_pump(void) {
  if (v7_init() != 0) return 0;
  drain_microtasks(g_vm);
  return run_one_event(g_vm, 0);
}
V7_KEEPALIVE int v7_next_timeout_ms(void) {
  uint64_t now = runtime_now_ms(), best = UINT64_MAX;
  if (v7_init() != 0) return -1;
  for (unsigned i = 0; i < JS_TIMER_MAX; i++) if (g_vm->timers[i].active && g_vm->timers[i].due_ms < best) best = g_vm->timers[i].due_ms;
  if (best == UINT64_MAX) return -1;
  if (best <= now) return 0;
  return best - now > 2147483647ULL ? 2147483647 : (int) (best - now);
}


static int eval_and_print(const char *source, size_t length) {
  char *result;
  (void) length; /* source is NUL-terminated by the caller */
  result = v7_eval(source);
  if (result == NULL) {
    fputs("V7: evaluation failed (out of memory or VM init error)\n", stderr);
    return 1;
  }
  puts(result);
  __builtin_free(result);
  return g_last_status;
}

#ifndef __EMSCRIPTEN__
static char *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  long end;
  char *data;
  size_t got;

  if (file == NULL) {
    fprintf(stderr, "V7: cannot open '%s'\n", path);
    return NULL;
  }  if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fprintf(stderr, "V7: cannot size '%s'\n", path);
    fclose(file);
    return NULL;
  }
  data = (char *) malloc((size_t) end + 1U);
  if (data == NULL) {
    fprintf(stderr, "V7: out of memory reading '%s'\n", path);
    fclose(file);
    return NULL;
  }
  got = fread(data, 1, (size_t) end, file);
  if (got != (size_t) end || ferror(file)) {
    fprintf(stderr, "V7: failed reading '%s'\n", path);
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  data[got] = '\0';
  *length = got;
  return data;
}
#endif


/* Built-in REPL examples. Keep these compatible with the regression-tested subset. */
static void print_example(int number) {
  static const char *code[] = {
    "6 * 7;",
    "let name = \"V7\"; name + \" Turbo\";",
    "function square(x) { return x * x; } square(9);",
    "const nums = [2, 4, 6]; nums[1];",
    "const user = { name: \"zenzai\", level: 7 }; user.name;",
    "const {x, y} = {x: 3, y: 8}; x + y;",
    "const a = [1, 2]; const b = [3, 4]; [...a, ...b].length;",
    "let value = null; value ?? \"fallback\";",
    "await Promise.resolve(7).then(function(n) { return n + 1; });",
    "V7.setMode(\"Turbo\"); V7.getMode();"
  };
  static const char *expected[] = {
    "42",
    "\"V7\"",
    "81",
    "4",
    "\"zenzai\"",
    "11",
    "4",
    "\"fallback\"",
    "8",
    "\"Turbo\""
  };
  if (number < 1 || number > 10) return;
  printf("Example %d\nCode: %s\nExpected: %s\n", number, code[number - 1], expected[number - 1]);
}

#ifndef V7_NO_MAIN
int main(int argc, char **argv) {
  int status = 0;

  if (v7_init() != 0) return 1;

#ifdef __EMSCRIPTEN__
  /* Browser builds use the exported v7_eval() API instead of stdin. */
  (void) argc;
  (void) argv;
  puts("V7 JavaScript engine ready.");
  return 0;
#else
  if (argc == 3 && strcmp(argv[1], "-e") == 0) {
    return eval_and_print(argv[2], strlen(argv[2]));
  }
  if (argc == 2) {
    size_t length = 0;
    char *source = read_file(argv[1], &length);
    if (source == NULL) return 1;
    status = eval_and_print(source, length);
    free(source);
    return status;
  }
  if (argc != 1) {
    fprintf(stderr, "Usage: %s [-e CODE | FILE]\n", argv[0]);
    return 2;
  }

  fputs("V7 REPL " JS_VERSION "\n"
        "Type :help for commands; Ctrl+D or :quit to leave.\n", stdout);
  for (;;) {
    char line[16384];
    char *cmd;
    size_t n;

    fputs("V7> ", stdout);
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL) {
      fputc('\n', stdout);
      break;
    }

    /* Trim the line in place for REPL meta-commands and blank-line checks. */
    cmd = line;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    n = strlen(cmd);
    while (n > 0 && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r' ||
                     cmd[n - 1] == ' ' || cmd[n - 1] == '\t')) {
      cmd[--n] = '\0';
    }
    if (n == 0) continue;

    if (strncmp(cmd, ":example", 8) == 0 &&
        (cmd[8] == '\0' || cmd[8] == ' ' || cmd[8] == '\t')) {
      char *arg = cmd + 8;
      char *end = NULL;
      long first, last;
      while (*arg == ' ' || *arg == '\t') arg++;
      if (*arg == '\0') {
        puts("Usage: :example 1-10 (or :example N)");
        continue;
      }
      first = strtol(arg, &end, 10);
      if (end == arg) {
        puts("Usage: :example 1-10 (or :example N)");
        continue;
      }
      while (*end == ' ' || *end == '\t') end++;
      last = first;
      if (*end == '-') {
        arg = end + 1;
        last = strtol(arg, &end, 10);
        if (end == arg) {
          puts("Usage: :example 1-10 (or :example N)");
          continue;
        }
        while (*end == ' ' || *end == '\t') end++;
      }
      if (*end != '\0' || first < 1 || last > 10 || last < first) {
        puts("Choose examples from 1 through 10; ranges such as :example 1-10 are supported.");
        continue;
      }
      for (long i = first; i <= last; i++) {
        print_example((int) i);
        if (i != last) putchar('\n');
      }
      continue;
    }
    if (strcmp(cmd, ":help") == 0 || strcmp(cmd, ".help") == 0) {
      puts("REPL commands:");
      puts("  :help / .help   Show this help");
      puts("  :clear / .clear Clear the terminal screen");
      puts("  :version        Show V7 version");
      puts("  :example 1-10   Print built-in JavaScript examples");
      puts("  :quit / .quit   Exit the REPL");
      puts("Runtime APIs: Promise, queueMicrotask, setTimeout/clearTimeout, setInterval/clearInterval, console.log/warn/error, readFile(path), writeFile(path, text).");
      puts("V7 accepts ECMAScript-style JavaScript source; see docs/javascript-support.md for supported syntax and limitations. V7.setMode(\"Safe\"|\"Turbo\"|\"Extreme\") is an optional engine tuning API, not a language syntax.");
      puts("Submit one statement or expression per line.");
      continue;
    }
    if (strcmp(cmd, ":quit") == 0 || strcmp(cmd, ".quit") == 0 ||
        strcmp(cmd, ":q") == 0 || strcmp(cmd, ".q") == 0) {
      break;
    }
    if (strcmp(cmd, ":clear") == 0 || strcmp(cmd, ".clear") == 0) {
      fputs("\033[2J\033[H", stdout);
      fflush(stdout);
      continue;
    }
    if (strcmp(cmd, ":version") == 0) {
      printf("V7 %s\n", JS_VERSION);
      continue;
    }

    if (eval_and_print(cmd, n) != 0) status = 1;
  }
  return status;
#endif
}
#endif /* V7_NO_MAIN */