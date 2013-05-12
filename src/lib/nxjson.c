/*
 * Copyright (c) 2013 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXJSON.
 *
 * NXJSON is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXJSON is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXJSON. If not, see <http://www.gnu.org/licenses/>.
 */

// this file can be #included in your code
#ifndef NXJSON_C
#define NXJSON_C

#ifdef  __cplusplus
extern "C" {
#endif


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#include "nxjson.h"

// redefine NX_JSON_CALLOC & NX_JSON_FREE to use custom allocator
#ifndef NX_JSON_CALLOC
#define NX_JSON_CALLOC() calloc(1, sizeof(nx_json))
#define NX_JSON_FREE(json) free((void*)(json))
#endif

// redefine NX_JSON_REPORT_ERROR to use custom error reporting
#ifndef NX_JSON_REPORT_ERROR
#define NX_JSON_REPORT_ERROR(msg, p) fprintf(stderr, "NXJSON PARSE ERROR (%d): " msg " at %s\n", __LINE__, p)
#endif

#define IS_WHITESPACE(c) ((unsigned char)(c)<=(unsigned char)' ')

static const nx_json dummy={ NX_JSON_NULL };

static nx_json* create_json(nx_json_type type, const char* key, nx_json* parent) {
  nx_json* js=NX_JSON_CALLOC();
  assert(js);
  js->type=type;
  js->key=key;
  if (!parent->last_child) {
    parent->child=parent->last_child=js;
  }
  else {
    parent->last_child->next=js;
    parent->last_child=js;
  }
  parent->length++;
  return js;
}

void nx_json_free(const nx_json* js) {
  nx_json* p=js->child;
  nx_json* p1;
  while (p) {
    p1=p->next;
    nx_json_free(p);
    p=p1;
  }
  NX_JSON_FREE(js);
}

static char* unescape_string(char* s, char** end) {
  char* p=s;
  char* d=s;
  char c;
  while ((c=*p++)) {
    if (c=='"') {
      *d='\0';
      *end=p;
      return s;
    }
    else if (c=='\\') {
      switch (*p) {
        case '\\':
        case '/':
        case '"':
          c=*p++;
          break;
        case 'b':
          c='\b'; p++;
          break;
        case 'f':
          c='\f'; p++;
          break;
        case 'n':
          c='\n'; p++;
          break;
        case 'r':
          c='\r'; p++;
          break;
        case 't':
          c='\t'; p++;
          break;
        case 'u': // unicode unescape not implemented
          break;
      }
      *d++=c;
    }
    else {
      *d++=c;
    }
  }
  NX_JSON_REPORT_ERROR("no closing quote for string", s);
  return 0;
}

static char* parse_key(const char** key, char* p) {
  // on '}' return with *p=='}'
  char c;
  while ((c=*p++)) {
    if (c=='"') {
      *key=unescape_string(p, &p);
      if (!*key) return 0; // propagate error
      while (*p && IS_WHITESPACE(*p)) p++;
      if (*p==':') return p+1;
      NX_JSON_REPORT_ERROR("unexpected chars", p);
      return 0;
    }
    else if (IS_WHITESPACE(c) || c==',') {
      // continue
    }
    else if (c=='}') {
      return p-1;
    }
    else if (c=='/') {
      if (*p=='/') { // line comment
        char* ps=p-1;
        p=strchr(p+1, '\n');
        if (!p) {
          NX_JSON_REPORT_ERROR("endless comment", ps);
          return 0; // error
        }
        p++;
      }
      else if (*p=='*') { // block comment
        char* ps=p-1;
        REPEAT2:
        p=strchr(p+1, '/');
        if (!p) {
          NX_JSON_REPORT_ERROR("endless comment", ps);
          return 0; // error
        }
        if (p[-1]!='*') {
          goto REPEAT2;
        }
        p++;
      }
      else {
        NX_JSON_REPORT_ERROR("unexpected chars", p-1);
        return 0; // error
      }
    }
    else {
      NX_JSON_REPORT_ERROR("unexpected chars", p-1);
      return 0; // error
    }
  }
  NX_JSON_REPORT_ERROR("unexpected chars", p-1);
  return 0; // error
}

static char* parse_value(nx_json* parent, const char* key, char* p) {
  nx_json* js;
  while (1) {
    switch (*p) {
      case '\0':
        NX_JSON_REPORT_ERROR("unexpected end of text", p);
        return 0; // error
      case ' ': case '\t': case '\n': case '\r':
      case ',':
        // skip
        p++;
        break;
      case '{':
        js=create_json(NX_JSON_OBJECT, key, parent);
        p++;
        while (1) {
          const char* new_key;
          p=parse_key(&new_key, p);
          if (!p) return 0; // error
          if (*p=='}') return p+1; // end of object
          p=parse_value(js, new_key, p);
          if (!p) return 0; // error
        }
      case '[':
        js=create_json(NX_JSON_ARRAY, key, parent);
        p++;
        while (1) {
          p=parse_value(js, 0, p);
          if (!p) return 0; // error
          if (*p==']') return p+1; // end of array
        }
      case ']':
        return p;
      case '"':
        p++;
        js=create_json(NX_JSON_STRING, key, parent);
        js->text_value=unescape_string(p, &p);
        if (!js->text_value) return 0; // propagate error
        return p;
      case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        {
          js=create_json(NX_JSON_INTEGER, key, parent);
          char* pe;
          js->int_value=strtol(p, &pe, 0);
          if (pe==p) {
            NX_JSON_REPORT_ERROR("invalid number", p);
            return 0; // error
          }
          if (*pe=='.' || *pe=='e' || *pe=='E') { // double value
            js->type=NX_JSON_DOUBLE;
            js->dbl_value=strtod(p, &pe);
            if (pe==p) {
              NX_JSON_REPORT_ERROR("invalid number", p);
              return 0; // error
            }
          }
          else {
            js->dbl_value=js->int_value;
          }
          return pe;
        }
      case 't':
        if (!strncmp(p, "true", 4)) {
          js=create_json(NX_JSON_BOOL, key, parent);
          js->int_value=1;
          return p+4;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case 'f':
        if (!strncmp(p, "false", 5)) {
          js=create_json(NX_JSON_BOOL, key, parent);
          js->int_value=0;
          return p+5;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case 'n':
        if (!strncmp(p, "null", 4)) {
          create_json(NX_JSON_NULL, key, parent);
          return p+4;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case '/': // comment
        if (p[1]=='/') { // line comment
          char* ps=p;
          p=strchr(p+2, '\n');
          if (!p) {
            NX_JSON_REPORT_ERROR("endless comment", ps);
            return 0; // error
          }
          p++;
        }
        else if (p[1]=='*') { // block comment
          char* ps=p;
          REPEAT2:
          p=strchr(p+2, '/');
          if (!p) {
            NX_JSON_REPORT_ERROR("endless comment", ps);
            return 0; // error
          }
          if (p[-1]!='*') {
            goto REPEAT2;
          }
          p++;
        }
        else {
          NX_JSON_REPORT_ERROR("unexpected chars", p);
          return 0; // error
        }
        break;
      default:
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
    }
  }
}

const nx_json* nx_json_parse(char* text) {
  nx_json js={0};
  if (!parse_value(&js, 0, text)) {
    if (js.child) nx_json_free(js.child);
    return 0;
  }
  return js.child;
}

const nx_json* nx_json_get(const nx_json* json, const char* key) {
  if (!json) return &dummy; // never return null
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (!strcmp(js->key, key)) return js;
  }
  return &dummy; // never return null
}

const nx_json* nx_json_item(const nx_json* json, int idx) {
  if (!json) return &dummy; // never return null
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (!idx--) return js;
  }
  return &dummy; // never return null
}

#ifdef NXJSON_DEMO

int main() {
  char* code=strdup(" {\"some-int\":195, \"array\" :[ 0, 5.1, -7, \"\\\\\" ,, /*11*/ , \"last\\nitem\"],"
    "\"some-bool\":true, \"some-dbl\":-1e-4, \"some-null\": null, \"hello\" : \"world\\\"\\!\", /*\"other\" : \"/OTHER/\"*/,\n"
    "\"obj\":{\"KEY\":\"VAL\"}\n"
    "}");
  const nx_json* json=nx_json_parse(code);
  if (json) {
    printf("some-int=%ld\n", nx_json_get(json, "some-int")->int_value);
    printf("some-dbl=%lf\n", nx_json_get(json, "some-dbl")->dbl_value);
    printf("some-bool=%ld\n", nx_json_get(json, "some-bool")->int_value);
    printf("some-null=%s\n", nx_json_get(json, "some-null")->text_value);
    printf("hello=%s\n", nx_json_get(json, "hello")->text_value);
    printf("KEY=%s\n", nx_json_get(nx_json_get(json, "obj"), "KEY")->text_value);
    printf("other=%s\n", nx_json_get(json, "other")->text_value);
    const nx_json* arr=nx_json_get(json, "array");
    int i;
    for (i=0; i<arr->length; i++) {
      const nx_json* item=nx_json_item(arr, i);
      printf("arr[%d]=(%d) %ld %lf %s\n", i, (int)item->type, item->int_value, item->dbl_value, item->text_value);
    }
    nx_json_free(json);
  }
  free(code);
  return 0;
}

#endif


#ifdef  __cplusplus
}
#endif

#endif  /* NXJSON_C */
