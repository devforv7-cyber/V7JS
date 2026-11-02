//this is optional, you dont have to use v7.h unless if you need APIs
#ifndef V7_H_INCLUDED
#define V7_H_INCLUDED
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t jsval_t;
typedef struct js V7;
enum { JS_UNDEF, JS_NULL, JS_TRUE, JS_FALSE, JS_STR, JS_NUM, JS_ERR, JS_PRIV };
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
#ifdef __cplusplus
}
#endif
#endif
