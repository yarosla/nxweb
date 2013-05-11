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

#ifndef NXJSON_H
#define NXJSON_H

#ifdef  __cplusplus
extern "C" {
#endif


typedef enum nx_json_type {
  NX_JSON_NULL,
  NX_JSON_OBJECT,
  NX_JSON_ARRAY,
  NX_JSON_STRING,
  NX_JSON_INTEGER,
  NX_JSON_DOUBLE,
  NX_JSON_BOOL
} nx_json_type;

typedef struct nx_json {
  nx_json_type type;
  const char* tag;
  const char* text_value;
  long int_value; // also bool
  double dbl_value;
  int length; // num children
  struct nx_json* child;
  struct nx_json* next;
} nx_json;

const nx_json* nx_json_parse(char* text);
void nx_json_free(const nx_json* js);
const nx_json* nx_json_get(const nx_json* json, const char* tag); // get object's property by key
const nx_json* nx_json_item(const nx_json* json, int idx); // get array element by index


#ifdef  __cplusplus
}
#endif

#endif  /* NXJSON_H */
