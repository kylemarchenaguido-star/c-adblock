#pragma once
#include <cstddef> // NULL: this header has no wiwndows includes to bring it in

#define JSON_MAX_DEPTH 256

enum JsonType {
  JSON_NULL,
  JSON_BOOL,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT,
};

struct JsonMember;
 
// strcut for every JSON type, only the fields for type mean anything.
struct JsonValue {
  JsonType type = JSON_NULL;  // defaults to null so we can json_free
  int boolean = 0;            // JSON_BOOL
  double number = 0;          // JSON_NUMBER
  char *str = NULL;           // JSON_STRING, always NUL-terminated
  int str_len = 0;            // JSON_STRING- bytes before the NUL
  JsonValue *items = NULL;    // JSON_ARRAY
  JsonMember *members = NULL; // JSON_OBJECT
  int count = 0;              // JSON_ARRAY / JSON_OBJECT: entries in use
  int cap = 0;                // JSON_ARRAY / JSON_OBJECT: entries allocated
};

struct JsonMember {
  char *key;
  JsonValue value;
};

// on success *out owns the whole tree, release with json_free
// on failure *out is left as null and err (optional) says what went wrong and where
int json_parse(const char *text, int len, JsonValue *out, char *err, int err_cap);
void json_free(JsonValue *v);

// every lookup accepts NULL and returns NULL, so calls can be chained and check once at the end
JsonValue* json_get(JsonValue *obj, const char *key);
JsonValue* json_at(JsonValue *arr, int index);
const char* json_get_string(JsonValue *obj, const char *key);

// json_push/json_set take ownership of the value passed in, even when they
// fail, so never json_free a value after handing it over
JsonValue json_null();
JsonValue json_bool(int b);
JsonValue json_number(double n);
JsonValue json_string(const char *s);
JsonValue json_string_n(const char *s, int len);
JsonValue json_array();
JsonValue json_object();
int json_push(JsonValue *arr, JsonValue item);
int json_ste(JsonValue *obj, const char *key, JsonValue value);

// compact output, no whitespace, same shape as org.json's toString()
// returns a malloc'd NUL-terminated string (free() it), length in *out_len.
char* json_serialize(JsonValue *v, int *out_len);
