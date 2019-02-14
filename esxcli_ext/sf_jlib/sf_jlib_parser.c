/* vim: set et ts=4
 *
 * Copyright (C) 2015 Mirko Pasqualetti  All rights reserved.
 * https://github.com/udp/json-parser
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "json.h"
#include "sf_jlib.h"

#ifdef SF_JLIB_DEBUG
#define SF_JLIB_ERROR(fmt, ...)                                      \
  printf("%s:%d: ERROR: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define SF_JLIB_ERROR(fmt, ...)
#endif

typedef struct sf_fwinfo_list_s {
  struct sf_fwinfo_list_s *next;
  int fw_subtype;
  int fw_type;
  char fw_version[SF_JLIB_MAX_VER_STRING_LENGTH];
  char fw_filename[SF_JLIB_MAX_VER_STRING_LENGTH];
  char fw_filepath[SF_JLIB_MAX_FILE_PATH_LENGTH];
} sf_fwinfo_list_t;

static json_value *js_value = NULL;

static sf_fwinfo_list_t *bootrom_list = NULL;
static sf_fwinfo_list_t *controller_list = NULL;
static sf_fwinfo_list_t *uefirom_list = NULL;
static sf_fwinfo_list_t *sucfw_list = NULL;

static void process_value(json_value *value, int *rc, sf_fwinfo_list_t **fw_object, char *arg_name);

/***************************************************************************************************/

static void clean_list_nodes(sf_fwinfo_list_t *head)
{
  sf_fwinfo_list_t *last = head;

  while (last) {
    head = last->next;
    free(last);
    last = head;
  }

  return;
}

int process_fw_list(sf_fwinfo_list_t *head,
                    int fw_subtype, char *ver, char *path)
{
  sf_fwinfo_list_t *last;
  int copyLen = 0;

  if (head == NULL) {
    SF_JLIB_ERROR("List head is null\n");
    return -EPERM;
  }

  last = head;
  while (last) {
    if (last->fw_subtype == fw_subtype) {
      copyLen = strcspn(last->fw_version, "-");

      if (copyLen == 0)
        copyLen = SF_JLIB_MAX_VER_STRING_LENGTH;

      strncpy(ver, &last->fw_version[1], (copyLen - 1));
      strcpy(path, last->fw_filepath);
      return 0;
    }

    last = last->next;
  }

  return -ENOENT;
}

static void process_object(json_value* value, int *rc, sf_fwinfo_list_t **fw_object)
{
  int length, x;

  if (value == NULL)
    return;

  if (*rc > 0)
    return;

  length = value->u.object.length;

  for (x = 0; x < length; x++) {
    if (!strcmp("bootROM", value->u.object.values[x].name))
      process_value(value->u.object.values[x].value, rc, &bootrom_list, NULL);
    else if (!strcmp("controller", value->u.object.values[x].name))
      process_value(value->u.object.values[x].value, rc, &controller_list, NULL);
    else if (!strcmp("uefiROM", value->u.object.values[x].name))
      process_value(value->u.object.values[x].value, rc, &uefirom_list, NULL);
    else if (!strcmp("sucfw", value->u.object.values[x].name))
      process_value(value->u.object.values[x].value, rc, &sucfw_list, NULL);
    else if (!strcmp("files", value->u.object.values[x].name))
      process_value(value->u.object.values[x].value, rc, fw_object, NULL);
    else
      process_value(value->u.object.values[x].value, rc, fw_object, value->u.object.values[x].name);
  }

  return;
}

static void process_array(json_value* value, int *rc, sf_fwinfo_list_t **fw_object)
{
  sf_fwinfo_list_t *last = NULL;
  sf_fwinfo_list_t *new = NULL;

  int length, x;

  if (value == NULL)
    return;

  if (*rc > 0)
    return;

  length = value->u.array.length;

  for (x = 0; x < length; x++) {
    if ((new = calloc(1, sizeof(sf_fwinfo_list_t))) == NULL) {
      SF_JLIB_ERROR("Failed to allocate memory\n");
      *rc = 1;
      return;
    }

    if (*fw_object == NULL)
      *fw_object = last = new;
    else {
      last->next = new;
      last = new;
    }

    process_value(value->u.array.values[x], rc, &new, NULL);
  }

  return;
}

static void process_value(json_value* value, int *rc, sf_fwinfo_list_t **fw_object, char *arg_name)
{
  sf_fwinfo_list_t *node = NULL;

  if (value == NULL)
    return;

  if (*rc > 0)
    return;

  if (fw_object && *fw_object)
    node = *fw_object;

  switch (value->type) {
    case json_object:
      process_object(value, rc, fw_object);
      break;

    case json_array:
      process_array(value, rc, fw_object);
      break;

    case json_integer:
      if (!strcmp(arg_name, "subtype"))
        node->fw_subtype = (unsigned int)value->u.integer;
      else if (!strcmp(arg_name, "type"))
        node->fw_type = (unsigned int)value->u.integer;
      else {
        /* Unrecognised option */
        SF_JLIB_ERROR("Unrecognized option\n");
        *rc = 2;
      }

      break;

    case json_string:
      if (!strcmp(arg_name, "versionString"))
        strcpy(node->fw_version, value->u.string.ptr);
      else if (!strcmp(arg_name, "name"))
        strcpy(node->fw_filename, value->u.string.ptr);
      else if (!strcmp(arg_name, "path"))
        strcpy(node->fw_filepath, value->u.string.ptr);
      else {
        /* Unrecognised option */
        SF_JLIB_ERROR("Unrecognized option\n");
        *rc = 2;
      }

      break;

    case json_boolean:
    case json_none:
    case json_double:
    case json_null:
      /* Unrecognised type */
      SF_JLIB_ERROR("Unrecognized type\n");
      *rc = 3;
      break;
  }

  return;
}

int sf_jlib_init(char *filename)
{
  FILE *fp;
  char *file_contents;
  struct stat filestatus;
  int file_size;
  int rc = 0;

  if (filename == NULL) {
    SF_JLIB_ERROR("Invalid input parameter\n");
    return -EINVAL;
  }

  if (stat(filename, &filestatus) != 0) {
    SF_JLIB_ERROR("File %s not found\n", filename);
    return -ENOENT;
  }

  file_size = filestatus.st_size;
  file_contents = (char*)malloc(filestatus.st_size);
  if (file_contents == NULL) {
    SF_JLIB_ERROR("Memory error: unable to allocate %d bytes\n", file_size);
    return -ENOMEM;
  }

  fp = fopen(filename, "rt");
  if (fp == NULL) {
    SF_JLIB_ERROR("Unable to open %s\n", filename);
    fclose(fp);
    free(file_contents);
    return -ENOENT;
  }

  if (fread(file_contents, file_size, 1, fp) != 1 ) {
    SF_JLIB_ERROR("Unable to read content of %s\n", filename);
    fclose(fp);
    free(file_contents);
    return -EIO;
  }

  fclose(fp);

  js_value = json_parse((json_char *)file_contents, file_size);
  if (js_value == NULL) {
    SF_JLIB_ERROR("Unable to parse data\n");
    free(file_contents);
    return -EPERM;
  }

  process_value(js_value, &rc, NULL, NULL);

  free(file_contents);

  if (rc > 0) {
    sf_jlib_exit();
    return -EINVAL;
  }

  if ((controller_list == NULL) ||
      (bootrom_list    == NULL) ||
      (uefirom_list    == NULL)) {
    SF_JLIB_ERROR("Unable to initialize all list heads\n");
    return -EINVAL;
  }

  return 0;
}

void sf_jlib_exit(void)
{
  clean_list_nodes(controller_list);
  clean_list_nodes(bootrom_list);
  clean_list_nodes(uefirom_list);
  clean_list_nodes(sucfw_list);

  json_value_free(js_value);
}

int sf_jlib_find_image(sf_image_type_t imt,
                       int fw_subtype, char *ver, char *path)
{
  sf_fwinfo_list_t *fw_object = NULL;

  if (imt == uefirom)
    fw_object = uefirom_list;
  else if (imt == controller)
    fw_object = controller_list;
  else if (imt == bootrom)
    fw_object = bootrom_list;
  else if (imt == sucfw)
    fw_object = sucfw_list;
  else {
     SF_JLIB_ERROR("Invalid Firmware Type\n");
     return -EPERM;
  }

  return process_fw_list(fw_object, fw_subtype, ver, path);
}

#ifdef SF_JLIB_TEST_ALONE
void print_subtype(sf_image_type_t imt)
{
  sf_fwinfo_list_t *fw_object;

  if (imt == uefirom)
    fw_object = uefirom_list;
  else if (imt == controller)
    fw_object = controller_list;
  else if (imt == bootrom)
    fw_object = bootrom_list;
  else if (imt == sucfw)
    fw_object = sucfw_list;
  else {
     SF_JLIB_ERROR("Invalid Firmware Type\n");
     return;
  }

  while (fw_object) {
    printf(" %d", fw_object->fw_subtype);
    fw_object = fw_object->next;
  }

  printf("\n");
}

void print_list(sf_fwinfo_list_t *fwlist_head)
{

  if (!fwlist_head) {
    printf("Invalid firmware list head\n");
    return;
  }

  while(fwlist_head) {
    printf("Type:               %d\n", fwlist_head->fw_type);
    printf("Subtype:            %d\n", fwlist_head->fw_subtype);
    printf("Version:            %s\n", fwlist_head->fw_version);
    printf("Firmware File Name: %s\n", fwlist_head->fw_filename);
    printf("Firmware File Path: %s\n", fwlist_head->fw_filepath);
    printf("\n");
    fwlist_head = fwlist_head->next;
  }

}

int main(int argc, char** argv)
{
  char* filename;
  char ver[20];
  char path[64];
  int type = 0xffff;
  int subtype = 0xffff;
  int user_action = 0;
  int status;

  if (argc != 2) {
    SF_JLIB_ERROR("%s <file_json>\n", argv[0]);
    return 1;
  }

  filename = argv[1];

  status = sf_jlib_init(filename);
  if (status < 0)
    return 0;

  printf("Dump all entries/ Find an entry (1/2)\n");
  scanf("%d", &user_action);

  if (user_action == 1) {
    printf("********** CONTROLLER **********\n");
    print_list(controller_list);
    printf("\n");
    printf("*********** BOOTROM ************\n");
    print_list(bootrom_list);
    printf("\n");
    printf("*********** UEFIROM ***********\n");
    print_list(uefirom_list);
    printf("\n");
    printf("*********** SUCFW ***********\n");
    print_list(sucfw_list);
  } else if (user_action == 2) {
    memset(path, 0, 64);
    memset(ver, 0, 16);

    printf("Firmware image type: 0 (BootROM) 1 (Controller) 2 (UefiRom) 3 (SucFw)\n");
    scanf("%d", &type);
    printf("Firmware image subtype, available subtype are: \n");
    print_subtype(type);
    scanf("%d", &subtype);

    sf_jlib_find_image(type, subtype, ver, path);

    printf("Version: %s\n", ver);
    printf("PATH: %s\n", path);
  }

  sf_jlib_exit();

  return 0;
}
#endif
