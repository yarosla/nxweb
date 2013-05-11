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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "nxjson.h"

static const nx_json dummy={ NX_JSON_NULL };

static void append_json(nx_json* parent, nx_json* child) {
  nx_json* p=parent->child;
  if (!p) {
    parent->child=child;
  }
  else {
    while (p->next) p=p->next;
    p->next=child;
  }
  parent->length++;
}

static nx_json* create_json(nx_json_type type, const char* tag, nx_json* parent) {
  nx_json* js=calloc(1, sizeof(nx_json));
  js->type=type;
  js->tag=tag;
  append_json(parent, js);
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
  free((void*)js);
}

static char* unescape(char* s) {
  char* p;
  while (p=strchr(s, '\\')) {
    switch (p[1]) {
      // case '\\':
      // case '"':
      //   break;
      case 'b':
        *p='\b';
        break;
      case 'f':
        *p='\f';
        break;
      case 'n':
        *p='\n';
        break;
      case 'r':
        *p='\r';
        break;
      case 't':
        *p='\t';
        break;
      case 'u': // unicode unescape not implemented
        continue;
    }
    memmove(p, p+1, strlen(p)); // including null-terminator
  }
  return s;
}

static char* parse_tag(const char** tag, char* p) {
  // on '}' return with *p=='}'
  char c;
  while (c=*p++) {
    if (c=='"') {
      char* ps=p;
      REPEAT:
      p=strchr(p, '"');
      if (!p) {
        printf("ERROR: no closing quote for key %s\n", ps);
        return 0; // error
      }
      if (p[-1]=='\\') { // escaped
        p++;
        goto REPEAT;
      }
      *p++='\0';
      *tag=ps;
      while (*p && ((unsigned char)*p)<=' ') p++;
      return *p==':'? p+1 : 0;
    }
    else if (((unsigned char)c)<=' ' || c==',') {
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
          printf("ERROR: endless comment %s\n", ps);
          return 0; // error
        }
        p++;
      }
      else if (*p=='*') { // block comment
        char* ps=p-1;
        REPEAT2:
        p=strchr(p+1, '/');
        if (!p) {
          printf("ERROR: endless comment %s\n", ps);
          return 0; // error
        }
        if (p[-1]!='*') {
          goto REPEAT2;
        }
        p++;
      }
      else {
        printf("ERROR (%d) AT: %s\n", __LINE__, p);
        return 0; // error
      }
    }
    else {
      printf("ERROR (%d) AT: %s\n", __LINE__, p);
      return 0; // error
    }
  }
  printf("ERROR (%d) AT: %s\n", __LINE__, p);
  return 0; // error
}

static char* parse_value(nx_json* parent, const char* tag, char* p) {
  nx_json* js;
  while (1) {
    switch (*p) {
      case '\0':
        printf("ERROR (%d): unexpected end of text\n", __LINE__);
        return 0; // error
      case ' ': case '\t': case '\n': case '\r':
      case ',':
        // skip
        p++;
        break;
      case '{':
        js=create_json(NX_JSON_OBJECT, tag, parent);
        p++;
        while (1) {
          const char* new_tag;
          p=parse_tag(&new_tag, p);
          if (!p) return 0; // error
          if (*p=='}') return p+1; // end of object
          p=parse_value(js, new_tag, p);
          if (!p) return 0; // error
        }
      case '[':
        js=create_json(NX_JSON_ARRAY, tag, parent);
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
        char* ps=p;
        REPEAT:
        p=strchr(p, '"');
        if (!p) {
          printf("ERROR: no closing quote for string %s\n", ps);
          return 0; // error
        }
        if (p[-1]=='\\') { // escaped
          p++;
          goto REPEAT;
        }
        *p++='\0';
        js=create_json(NX_JSON_STRING, tag, parent);
        js->text_value=unescape(ps);
        return p;
      case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        {
          js=create_json(NX_JSON_INTEGER, tag, parent);
          char* pe;
          js->int_value=strtol(p, &pe, 0);
          if (pe==p) {
            printf("ERROR (%d): invalid number AT: %s\n", __LINE__, p);
            return 0; // error
          }
          if (*pe=='.' || *pe=='e' || *pe=='E') { // double value
            js->type=NX_JSON_DOUBLE;
            js->dbl_value=strtod(p, &pe);
            if (pe==p) {
              printf("ERROR (%d): invalid number AT: %s\n", __LINE__, p);
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
          js=create_json(NX_JSON_BOOL, tag, parent);
          js->int_value=1;
          return p+4;
        }
        printf("ERROR (%d) AT: %s\n", __LINE__, p);
        return 0; // error
      case 'f':
        if (!strncmp(p, "false", 5)) {
          js=create_json(NX_JSON_BOOL, tag, parent);
          js->int_value=0;
          return p+5;
        }
        printf("ERROR (%d) AT: %s\n", __LINE__, p);
        return 0; // error
      case 'n':
        if (!strncmp(p, "null", 4)) {
          create_json(NX_JSON_NULL, tag, parent);
          return p+4;
        }
        printf("ERROR (%d) AT: %s\n", __LINE__, p);
        return 0; // error
      case '/': // comment
        if (p[1]=='/') { // line comment
          char* ps=p;
          p=strchr(p+2, '\n');
          if (!p) {
            printf("ERROR: endless comment %s\n", ps);
            return 0; // error
          }
          p++;
        }
        else if (p[1]=='*') { // block comment
          char* ps=p;
          REPEAT2:
          p=strchr(p+2, '/');
          if (!p) {
            printf("ERROR: endless comment %s\n", ps);
            return 0; // error
          }
          if (p[-1]!='*') {
            goto REPEAT2;
          }
          p++;
        }
        else {
          printf("ERROR (%d) AT: %s\n", __LINE__, p);
          return 0; // error
        }
        break;
      default:
        printf("ERROR (%d) AT: %s\n", __LINE__, p);
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

const nx_json* nx_json_get(const nx_json* json, const char* tag) {
  if (!json) return &dummy;
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (!strcmp(js->tag, tag)) return js;
  }
  return &dummy;
}

const nx_json* nx_json_item(const nx_json* json, int idx) {
  if (!json) return &dummy;
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (!idx--) return js;
  }
  return &dummy;
}

#ifdef NXJSON_DEBUG

int main() {
  char* code=strdup(" {\"some-int\":195, \"array\" :[ 0, 5.1, -7, \"nine\" ,, /*11*/ , ],"
    "\"some-bool\":true, \"some-dbl\":-1e-4, \"some-null\": null, \"hello\" : \"world\\\"!\", /*\"other\" : \"/OTHER/\"*/,\n"
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
