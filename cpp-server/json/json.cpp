#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void json_free(JsonValue *v){
  if (!v){ return; }
  switch (v->type) {
    case JSON_STRING:
      free(v->str);
      break;
    case JSON_ARRAY:
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

