#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

void json_free(JsonValue *v){
  if (!v){ return; }
  switch (v->type) {
    case JSON_STRING:
      free(v->str);
      break;
    case JSON_ARRAY:
      for (int i = 0; i < v->count; ++i){ json_free(&v->items[i]); }
      free(v->items);
      break;
    case JSON_OBJECT:
      for (int i = 0; i < v->count; ++i){
        free(v->members[i].key);
        json_free(&v->members[i].value);
      }
      free(v->members);
      break;
    default:
      break;
  }
  *v = JsonValue{}; // back to a plain null, so a second json_free is harmless
}

JsonValue* json_get(JsonValue *obj, const char *key){
  if (!obj || obj->type != JSON_OBJECT || !key){ return NULL; }
  for (int i = 0; i < obj->count; ++i){
    if (strcmp(obj->members[i].key, key) == 0){ return &obj->members[i].value; }
  }
  return NULL;
}

JsonValue* json_at(JsonValue *arr, int index){
  if (!arr || arr->type != JSON_ARRAY || index < 0 || index >= arr->count){ return NULL; }
  return &arr->items[index];
}

const char* json_get_string(JsonValue *obj, const char *key){
  JsonValue *v = json_get(obj, key);
  return (v && v->type == JSON_STRING) ? v->str : NULL;
}

JsonValue json_null(){
  return JsonValue{};
}

JsonValue json_bool(int b){
  JsonValue v;
  v.type = JSON_BOOL;
  v.boolean = b ? 1 : 0;
  return v;
}

JsonValue json_number(double n){
  JsonValue v;
  v.type = JSON_NUMBER;
  v.number = n;
  return v;
}

JsonValue json_string_n(const char *s, int len){
  JsonValue v;
  if (!s || len < 0){ return v; }
  char *copy = (char *)malloc(len + 1);
  if (!copy){ return v; }
  memcpy(copy, s, len);
  copy[len] = '\0';
  v.type = JSON_STRING;
  v.str = copy;
  v.str_len = len;
  return v;
}

JsonValue json_string(const char *s){
  return json_string_n(s, s ? (int)strlen(s) : 0);
}

JsonValue json_array(){
  JsonValue v;
  v.type = JSON_ARRAY;
  return v;
}

JsonValue json_object(){
  JsonValue v;
  v.type= JSON_OBJECT;
  return v;
}

int json_push(JsonValue *arr, JsonValue item){
  if (!arr || arr->type != JSON_ARRAY){ json_free(&item); return 0; }
  if (arr->count == arr->cap){
    int new_cap = arr->cap ? arr->cap * 2 : 4;
    JsonValue *grown = (JsonValue *)realloc(arr->items, new_cap * sizeof(JsonValue));
    if (!grown){ json_free(&item); return 0; }
    arr->items = grown;
    arr->cap = new_cap;
  }
  arr->items[arr->count++] = item;
  return 1;
}

// takes ownership of an already malloc'd key
static int object_append(JsonValue *obj, char *key, JsonValue value){
  if (obj->count == obj->cap){
    int new_cap = obj->cap ? obj->cap * 2 : 4;
    JsonMember *grown = (JsonMember *)realloc(obj->members, new_cap * sizeof(JsonMember));
    if (!grown){ free(key); json_free(&value); return 0; }
    obj->members = grown;
    obj->cap = new_cap;
  }
  obj->members[obj->count].key = key;
  obj->members[obj->count].value = value;
  obj->count++;
  return 1;
}

// like org.json's put(): replcaes the value in place if the key already exists
int json_set(JsonValue *obj, const char *key, JsonValue value){
  if (!obj || obj->type != JSON_OBJECT || !key){ json_free(&value); return 0; }
  for (int i = 0; i < obj->count; ++i){
    if (strcmp(obj->members[i].key, key) == 0){
      json_free(&obj->members[i].value);
      obj->members[i].value = value;
      return 1;
    }
  }
  int n = (int)strlen(key);
  char *copy = (char *)malloc(n + 1);
  if (!copy){ json_free(&value); return 0; }
  memcpy(copy, key, n + 1);
  return object_append(obj, copy, value);
}

struct JsonParser {
  const char *start; // for error offsets 
  const char *p;
  const char *end;
  char *err;
  int err_cap;
  int depth;
};

static int fail(JsonParser *ps, const char *what){
  if (ps->err && ps->err_cap > 0){
    snprintf(ps->err, ps->err_cap, "%s at offset %d", what, (int)(ps->p - ps->start));
  }
  return 0;
}

static void skip_ws(JsonParser *ps){
  while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')){
    ps->p++;
  }
}

static int is_digit(char c){
  return c >= '0' && c <= '9';
}

static int match_literal(JsonParser *ps, const char *lit){
  int n = (int)strlen(lit);
  if (ps->end - ps->p < n || memcmp(ps->p, lit, n) != 0){ return 0; }
  ps->p += n;
  return 1;
}

static int parse_value(JsonParser *ps, JsonValue *out);

static int parse_number(JsonParser *ps, JsonValue *out){

}
