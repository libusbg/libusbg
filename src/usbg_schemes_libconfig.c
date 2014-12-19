/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>

#include "usbg/usbg_internal.h"

#define USBG_NAME_TAG "name"
#define USBG_ATTRS_TAG "attrs"
#define USBG_STRINGS_TAG "strings"
#define USBG_FUNCTIONS_TAG "functions"
#define USBG_CONFIGS_TAG "configs"
#define USBG_LANG_TAG "lang"
#define USBG_TYPE_TAG "type"
#define USBG_INSTANCE_TAG "instance"
#define USBG_ID_TAG "id"
#define USBG_FUNCTION_TAG "function"
#define USBG_TAB_WIDTH 4

static inline int generate_function_label(usbg_function *f, char *buf, int size)
{
	return snprintf(buf, size, "%s_%s",
			 usbg_get_function_type_str(f->type), f->instance);

}

static int usbg_export_binding(usbg_binding *b, config_setting_t *root)
{
	config_setting_t *node;
	int ret = USBG_ERROR_NO_MEM;
	int cfg_ret;
	char label[USBG_MAX_NAME_LENGTH];
	int nmb;

#define CRETAE_ATTR_STRING(SOURCE, NAME)				\
	do {								\
		node = config_setting_add(root, NAME, CONFIG_TYPE_STRING); \
		if (!node)						\
			goto out;					\
		cfg_ret = config_setting_set_string(node, SOURCE);	\
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
	} while (0)

	CRETAE_ATTR_STRING(b->name, USBG_NAME_TAG);

	nmb = generate_function_label(b->target, label, sizeof(label));
	if (nmb >= sizeof(label)) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	CRETAE_ATTR_STRING(label, USBG_FUNCTION_TAG);

#undef CRETAE_ATTR_STRING

	ret = USBG_SUCCESS;

out:
	return ret;
}

static int usbg_export_config_bindings(usbg_config *c, config_setting_t *root)
{
	usbg_binding *b;
	config_setting_t *node;
	int ret = USBG_SUCCESS;

	TAILQ_FOREACH(b, &c->bindings, bnode) {
		node = config_setting_add(root, NULL, CONFIG_TYPE_GROUP);
		if (!node) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}

		ret = usbg_export_binding(b, node);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_export_config_strs_lang(usbg_config *c, char *lang_str,
					config_setting_t *root)
{
	config_setting_t *node;
	usbg_config_strs strs;
	int lang;
	int usbg_ret, cfg_ret, ret2;
	int ret = USBG_ERROR_NO_MEM;

	ret2 = sscanf(lang_str, "%x", &lang);
	if (ret2 != 1) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	usbg_ret = usbg_get_config_strs(c, lang, &strs);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_LANG_TAG, CONFIG_TYPE_INT);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_format(node, CONFIG_FORMAT_HEX);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	cfg_ret = config_setting_set_int(node, lang);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	node = config_setting_add(root, "configuration" , CONFIG_TYPE_STRING);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_string(node, strs.configuration);

	ret = cfg_ret == CONFIG_TRUE ? USBG_SUCCESS : USBG_ERROR_OTHER_ERROR;
out:
	return ret;

}

static int usbg_export_config_strings(usbg_config *c, config_setting_t *root)
{
	config_setting_t *node;
	int usbg_ret = USBG_SUCCESS;
	int nmb, i;
	int ret = USBG_ERROR_NO_MEM;
	char spath[USBG_MAX_PATH_LENGTH];
	struct dirent **dent;

	nmb = snprintf(spath, sizeof(spath), "%s/%s/%s", c->path,
		       c->name, STRINGS_DIR);
	if (nmb >= sizeof(spath)) {
		ret = USBG_ERROR_PATH_TOO_LONG;
		goto out;
	}

	nmb = scandir(spath, &dent, file_select, alphasort);
	if (nmb < 0) {
		ret = usbg_translate_error(errno);
		goto out;
	}

	for (i = 0; i < nmb; ++i) {
		node = config_setting_add(root, NULL, CONFIG_TYPE_GROUP);
		if (!node)
			goto out;

		usbg_ret = usbg_export_config_strs_lang(c, dent[i]->d_name,
							node);
		if (usbg_ret != USBG_SUCCESS)
			break;

		free(dent[i]);
	}
	/* This loop will be executed only if error occurred in previous one */
	for (; i < nmb; ++i)
		free(dent[i]);

	free(dent);
	ret = usbg_ret;
out:
	return ret;
}

static int usbg_export_config_attrs(usbg_config *c, config_setting_t *root)
{
	config_setting_t *node;
	usbg_config_attrs attrs;
	int usbg_ret, cfg_ret;
	int ret = USBG_ERROR_NO_MEM;

	usbg_ret = usbg_get_config_attrs(c, &attrs);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

#define ADD_CONFIG_ATTR(attr_name)					\
	do {								\
		node = config_setting_add(root, #attr_name, CONFIG_TYPE_INT); \
		if (!node)						\
			goto out;					\
		cfg_ret = config_setting_set_format(node, CONFIG_FORMAT_HEX); \
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
		cfg_ret = config_setting_set_int(node, attrs.attr_name); \
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
	} while (0)

	ADD_CONFIG_ATTR(bmAttributes);
	ADD_CONFIG_ATTR(bMaxPower);

#undef ADD_CONFIG_ATTR

	ret = USBG_SUCCESS;
out:
	return ret;

}

/* This function does not export configuration id because it is a more
 * a porperty of gadget which contains this config than config itself */
static int usbg_export_config_prep(usbg_config *c, config_setting_t *root)
{
	config_setting_t *node;
	int ret = USBG_ERROR_NO_MEM;
	int usbg_ret;
	int cfg_ret;

	node = config_setting_add(root, USBG_NAME_TAG, CONFIG_TYPE_STRING);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_string(node, c->label);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	node = config_setting_add(root, USBG_ATTRS_TAG, CONFIG_TYPE_GROUP);
	if (!node)
		goto out;

	usbg_ret = usbg_export_config_attrs(c, node);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_STRINGS_TAG, CONFIG_TYPE_LIST);
	if (!node)
		goto out;

	usbg_ret = usbg_export_config_strings(c, node);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_FUNCTIONS_TAG, CONFIG_TYPE_LIST);
	if (!node)
		goto out;

	ret = usbg_export_config_bindings(c, node);
out:
	return ret;

}

static int usbg_export_gadget_configs(usbg_gadget *g, config_setting_t *root)
{
	usbg_config *c;
	config_setting_t *node, *id_node;
	int ret = USBG_SUCCESS;
	int cfg_ret;

	TAILQ_FOREACH(c, &g->configs, cnode) {
		node = config_setting_add(root, NULL, CONFIG_TYPE_GROUP);
		if (!node) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}

		id_node = config_setting_add(node, USBG_ID_TAG,
					     CONFIG_TYPE_INT);
		if (!id_node) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}

		cfg_ret = config_setting_set_int(id_node, c->id);
		if (cfg_ret != CONFIG_TRUE) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		ret = usbg_export_config_prep(c, node);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_export_f_net_attrs(usbg_f_net_attrs *attrs,
				      config_setting_t *root)
{
	config_setting_t *node;
	char *addr;
	char addr_buf[USBG_MAX_STR_LENGTH];
	int cfg_ret;
	int ret = USBG_ERROR_NO_MEM;

	node = config_setting_add(root, "dev_addr", CONFIG_TYPE_STRING);
	if (!node)
		goto out;

	addr = ether_ntoa_r(&attrs->dev_addr, addr_buf);
	cfg_ret = config_setting_set_string(node, addr);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	node = config_setting_add(root, "host_addr", CONFIG_TYPE_STRING);
	if (!node)
		goto out;

	addr = ether_ntoa_r(&attrs->host_addr, addr_buf);
	cfg_ret = config_setting_set_string(node, addr);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	node = config_setting_add(root, "qmult", CONFIG_TYPE_INT);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_int(node, attrs->qmult);
	ret = cfg_ret == CONFIG_TRUE ? 0 : USBG_ERROR_OTHER_ERROR;

	/* if name is read only so we don't export it */
out:
	return ret;

}

static int usbg_export_function_attrs(usbg_function *f, config_setting_t *root)
{
	config_setting_t *node;
	usbg_function_attrs f_attrs;
	int usbg_ret, cfg_ret;
	int ret = USBG_ERROR_NO_MEM;

	usbg_ret = usbg_get_function_attrs(f, &f_attrs);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

	switch (f->type) {
	case F_SERIAL:
	case F_ACM:
	case F_OBEX:
		node = config_setting_add(root, "port_num", CONFIG_TYPE_INT);
		if (!node)
			goto out;

		cfg_ret = config_setting_set_int(node, f_attrs.serial.port_num);
		ret = cfg_ret == CONFIG_TRUE ? 0 : USBG_ERROR_OTHER_ERROR;
		break;
	case F_ECM:
	case F_SUBSET:
	case F_NCM:
	case F_EEM:
	case F_RNDIS:
		ret = usbg_export_f_net_attrs(&f_attrs.net, root);
		break;
	case F_PHONET:
		/* Don't export ifname because it is read only */
		break;
	case F_FFS:
		/* We don't need to export ffs attributes
		 * due to instance name export */
		ret = USBG_SUCCESS;
		break;
	default:
		ERROR("Unsupported function type\n");
		ret = USBG_ERROR_NOT_SUPPORTED;
	}

out:
	return ret;
}

/* This function does not import instance name becuase this is more property
 * of a gadget than a function itselt */
static int usbg_export_function_prep(usbg_function *f, config_setting_t *root)
{
	config_setting_t *node;
	int ret = USBG_ERROR_NO_MEM;
	int cfg_ret;

	node = config_setting_add(root, USBG_TYPE_TAG, CONFIG_TYPE_STRING);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_string(node, usbg_get_function_type_str(
						    f->type));
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	node = config_setting_add(root, USBG_ATTRS_TAG, CONFIG_TYPE_GROUP);
	if (!node)
		goto out;

	ret = usbg_export_function_attrs(f, node);
out:
	return ret;
}


static int usbg_export_gadget_functions(usbg_gadget *g, config_setting_t *root)
{
	usbg_function *f;
	config_setting_t *node, *inst_node;
	int ret = USBG_SUCCESS;
	int cfg_ret;
	char label[USBG_MAX_NAME_LENGTH];
	char *func_label;
	int nmb;

	TAILQ_FOREACH(f, &g->functions, fnode) {
		if (f->label) {
			func_label = f->label;
		} else {
			nmb = generate_function_label(f, label, sizeof(label));
			if (nmb >= sizeof(label)) {
				ret = USBG_ERROR_OTHER_ERROR;
				break;
			}
			func_label = label;
		}

		node = config_setting_add(root, func_label, CONFIG_TYPE_GROUP);
		if (!node) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}

		/* Add instance name to identify in this gadget */
		inst_node = config_setting_add(node, USBG_INSTANCE_TAG,
					  CONFIG_TYPE_STRING);
		if (!inst_node) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}

		cfg_ret = config_setting_set_string(inst_node, f->instance);
		if (cfg_ret != CONFIG_TRUE) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		ret = usbg_export_function_prep(f, node);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_export_gadget_strs_lang(usbg_gadget *g, const char *lang_str,
					config_setting_t *root)
{
	config_setting_t *node;
	usbg_gadget_strs strs;
	int lang;
	int usbg_ret, cfg_ret;
	int ret = USBG_ERROR_NO_MEM;

	ret = sscanf(lang_str, "%x", &lang);
	if (ret != 1) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	usbg_ret = usbg_get_gadget_strs(g, lang, &strs);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_LANG_TAG, CONFIG_TYPE_INT);
	if (!node)
		goto out;

	cfg_ret = config_setting_set_format(node, CONFIG_FORMAT_HEX);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	cfg_ret = config_setting_set_int(node, lang);
	if (cfg_ret != CONFIG_TRUE) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

#define ADD_GADGET_STR(str_name, field)					\
	do {								\
		node = config_setting_add(root, str_name, CONFIG_TYPE_STRING); \
		if (!node)						\
			goto out;					\
		cfg_ret = config_setting_set_string(node, strs.field);	\
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
	} while (0)

	ADD_GADGET_STR("manufacturer", str_mnf);
	ADD_GADGET_STR("product", str_prd);
	ADD_GADGET_STR("serialnumber", str_ser);

#undef ADD_GADGET_STR
	ret = USBG_SUCCESS;
out:
	return ret;
}

static int usbg_export_gadget_strings(usbg_gadget *g, config_setting_t *root)
{
	config_setting_t *node;
	int usbg_ret = USBG_SUCCESS;
	int nmb, i;
	int ret = USBG_ERROR_NO_MEM;
	char spath[USBG_MAX_PATH_LENGTH];
	struct dirent **dent;

	nmb = snprintf(spath, sizeof(spath), "%s/%s/%s", g->path,
		       g->name, STRINGS_DIR);
	if (nmb >= sizeof(spath)) {
		ret = USBG_ERROR_PATH_TOO_LONG;
		goto out;
	}

	nmb = scandir(spath, &dent, file_select, alphasort);
	if (nmb < 0) {
		ret = usbg_translate_error(errno);
		goto out;
	}

	for (i = 0; i < nmb; ++i) {
		node = config_setting_add(root, NULL, CONFIG_TYPE_GROUP);
		if (!node)
			break;

		usbg_ret = usbg_export_gadget_strs_lang(g, dent[i]->d_name,
							node);
		if (usbg_ret != USBG_SUCCESS)
			break;

		free(dent[i]);
	}
	/* This loop will be executed only if error occurred in previous one */
	for (; i < nmb; ++i)
		free(dent[i]);

	free(dent);
	ret = usbg_ret;
out:
	return ret;
}

static int usbg_export_gadget_attrs(usbg_gadget *g, config_setting_t *root)
{
	config_setting_t *node;
	usbg_gadget_attrs attrs;
	int usbg_ret, cfg_ret;
	int ret = USBG_ERROR_NO_MEM;

	usbg_ret = usbg_get_gadget_attrs(g, &attrs);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

#define ADD_GADGET_ATTR(attr_name)					\
	do {								\
		node = config_setting_add(root, #attr_name, CONFIG_TYPE_INT); \
		if (!node)						\
			goto out;					\
		cfg_ret = config_setting_set_format(node, CONFIG_FORMAT_HEX); \
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
		cfg_ret = config_setting_set_int(node, attrs.attr_name); \
		if (cfg_ret != CONFIG_TRUE) {				\
			ret = USBG_ERROR_OTHER_ERROR;			\
			goto out;					\
		}							\
	} while (0)

	ADD_GADGET_ATTR(bcdUSB);
	ADD_GADGET_ATTR(bDeviceClass);
	ADD_GADGET_ATTR(bDeviceSubClass);
	ADD_GADGET_ATTR(bDeviceProtocol);
	ADD_GADGET_ATTR(bMaxPacketSize0);
	ADD_GADGET_ATTR(idVendor);
	ADD_GADGET_ATTR(idProduct);
	ADD_GADGET_ATTR(bcdDevice);

#undef ADD_GADGET_ATTR

	ret = 0;
out:
	return ret;
}

static int usbg_export_gadget_prep(usbg_gadget *g, config_setting_t *root)
{
	config_setting_t *node;
	int ret = USBG_ERROR_NO_MEM;
	int usbg_ret;

	/* We don't export name tag because name should be given during
	 * loading of gadget */

	node = config_setting_add(root, USBG_ATTRS_TAG, CONFIG_TYPE_GROUP);
	if (!node)
		goto out;

	usbg_ret = usbg_export_gadget_attrs(g, node);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_STRINGS_TAG,
				     CONFIG_TYPE_LIST);
	if (!node)
		goto out;

	usbg_ret = usbg_export_gadget_strings(g, node);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_FUNCTIONS_TAG,
				  CONFIG_TYPE_GROUP);
	if (!node)
		goto out;

	usbg_ret = usbg_export_gadget_functions(g, node);
	if (usbg_ret) {
		ret = usbg_ret;
		goto out;
	}

	node = config_setting_add(root, USBG_CONFIGS_TAG,
				     CONFIG_TYPE_LIST);
	if (!node)
		goto out;

	usbg_ret = usbg_export_gadget_configs(g, node);
	ret = usbg_ret;
out:
	return ret;
}

/* Export gadget/function/config API implementation */

int usbg_export_function(usbg_function *f, FILE *stream)
{
	config_t cfg;
	config_setting_t *root;
	int ret;

	if (!f || !stream)
		return USBG_ERROR_INVALID_PARAM;

	config_init(&cfg);

	/* Set format */
	config_set_tab_width(&cfg, USBG_TAB_WIDTH);

	/* Allways successful */
	root = config_root_setting(&cfg);

	ret = usbg_export_function_prep(f, root);
	if (ret != USBG_SUCCESS)
		goto out;

	config_write(&cfg, stream);
out:
	config_destroy(&cfg);
	return ret;
}

int usbg_export_config(usbg_config *c, FILE *stream)
{
	config_t cfg;
	config_setting_t *root;
	int ret;

	if (!c || !stream)
		return USBG_ERROR_INVALID_PARAM;

	config_init(&cfg);

	/* Set format */
	config_set_tab_width(&cfg, USBG_TAB_WIDTH);

	/* Allways successful */
	root = config_root_setting(&cfg);

	ret = usbg_export_config_prep(c, root);
	if (ret != USBG_SUCCESS)
		goto out;

	config_write(&cfg, stream);
out:
	config_destroy(&cfg);
	return ret;
}

int usbg_export_gadget(usbg_gadget *g, FILE *stream)
{
	config_t cfg;
	config_setting_t *root;
	int ret;

	if (!g || !stream)
		return USBG_ERROR_INVALID_PARAM;

	config_init(&cfg);

	/* Set format */
	config_set_tab_width(&cfg, USBG_TAB_WIDTH);

	/* Allways successful */
	root = config_root_setting(&cfg);

	ret = usbg_export_gadget_prep(g, root);
	if (ret != USBG_SUCCESS)
		goto out;

	config_write(&cfg, stream);
out:
	config_destroy(&cfg);
	return ret;
}

#define usbg_config_is_int(node) (config_setting_type(node) == CONFIG_TYPE_INT)
#define usbg_config_is_string(node) \
	(config_setting_type(node) == CONFIG_TYPE_STRING)

static int split_function_label(const char *label, usbg_function_type *type,
				const char **instance)
{
	const char *floor;
	char buf[USBG_MAX_NAME_LENGTH];
	int len;
	int function_type;
	int ret = USBG_ERROR_NOT_FOUND;

	/* We assume that function type string doesn't contain '_' */
	floor = strchr(label, '_');
	/* if phrase before _ is longer than max name length we may
	 * stop looking */
	len = floor - label;
	if (len >= USBG_MAX_NAME_LENGTH || floor == label)
		goto out;

	strncpy(buf, label, len);
	buf[len] = '\0';

	function_type = usbg_lookup_function_type(buf);
	if (function_type < 0)
		goto out;

	*type = (usbg_function_type)function_type;
	*instance = floor + 1;

	ret = USBG_SUCCESS;
out:
	return ret;
}

static void usbg_set_failed_import(config_t **to_set, config_t *failed)
{
	if (*to_set != NULL) {
		config_destroy(*to_set);
		free(*to_set);
	}

	*to_set = failed;
}

static int usbg_import_f_net_attrs(config_setting_t *root, usbg_function *f)
{
	config_setting_t *node;
	int ret = USBG_SUCCESS;
	int qmult;
	struct ether_addr *addr;
	struct ether_addr addr_buf;
	const char *str;

#define GET_OPTIONAL_ADDR(NAME)					\
	do {							\
		node = config_setting_get_member(root, #NAME);	\
		if (node) {					\
			str = config_setting_get_string(node);	\
			if (!str) {				\
				ret = USBG_ERROR_INVALID_TYPE;	\
				goto out;			\
			}					\
								\
			addr = ether_aton_r(str, &addr_buf);	\
			if (!addr) {				\
				ret = USBG_ERROR_INVALID_VALUE;	\
				goto out;			\
			}					\
			ret = usbg_set_net_##NAME(f, addr);	\
			if (ret != USBG_SUCCESS)		\
				goto out;			\
		}						\
	} while (0)

	GET_OPTIONAL_ADDR(host_addr);
	GET_OPTIONAL_ADDR(dev_addr);

#undef GET_OPTIONAL_ADDR

	node = config_setting_get_member(root, "qmult");
	if (node) {
		if (!usbg_config_is_int(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto out;
		}
		qmult = config_setting_get_int(node);
		ret = usbg_set_net_qmult(f, qmult);
	}

out:
	return ret;
}

static int usbg_import_function_attrs(config_setting_t *root, usbg_function *f)
{
	int ret = USBG_SUCCESS;

	switch (f->type) {
	case F_SERIAL:
	case F_ACM:
	case F_OBEX:
		/* Don't import port_num because it is read only */
		break;
	case F_ECM:
	case F_SUBSET:
	case F_NCM:
	case F_EEM:
	case F_RNDIS:
		ret = usbg_import_f_net_attrs(root, f);
		break;
	case F_PHONET:
		/* Don't import ifname because it is read only */
		break;
	case F_FFS:
		/* We don't need to export ffs attributes
		 * due to instance name export */
		break;
	default:
		ERROR("Unsupported function type\n");
		ret = USBG_ERROR_NOT_SUPPORTED;
	}

	return ret;
}

static int usbg_import_function_run(usbg_gadget *g, config_setting_t *root,
				    const char *instance, usbg_function **f)
{
	config_setting_t *node;
	const char *type_str;
	int usbg_ret;
	int function_type;
	int ret = USBG_ERROR_MISSING_TAG;

	/* function type is mandatory */
	node = config_setting_get_member(root, USBG_TYPE_TAG);
	if (!node)
		goto out;

	type_str = config_setting_get_string(node);
	if (!type_str) {
		ret = USBG_ERROR_INVALID_TYPE;
		goto out;
	}

	/* Check if this type is supported */
	function_type = usbg_lookup_function_type(type_str);
	if (function_type < 0) {
		ret = USBG_ERROR_NOT_SUPPORTED;
		goto out;
	}

	/* All data collected, let's get to work and create this function */
	ret = usbg_create_function(g, (usbg_function_type)function_type,
				   instance, NULL, f);

	if (ret != USBG_SUCCESS)
		goto out;

	/* Attrs are optional */
	node = config_setting_get_member(root, USBG_ATTRS_TAG);
	if (node) {
		usbg_ret = usbg_import_function_attrs(node, *f);
		if (usbg_ret != USBG_SUCCESS) {
			ret = usbg_ret;
			goto out;
		}
	}
out:
	return ret;
}

static usbg_function *usbg_lookup_function(usbg_gadget *g, const char *label)
{
	usbg_function *f;
	int usbg_ret;

	/* check if such function has also been imported */
	TAILQ_FOREACH(f, &g->functions, fnode) {
		if (f->label && !strcmp(f->label, label))
			break;
	}

	/* if not let's check if label follows the naming convention */
	if (!f) {
		usbg_function_type type;
		const char *instance;

		usbg_ret = split_function_label(label, &type, &instance);
		if (usbg_ret != USBG_SUCCESS)
			goto out;

		/* check if such function exist */
		f = usbg_get_function(g, type, instance);
	}

out:
	return f;
}

/* We have a string which should match with one of function names */
static int usbg_import_binding_string(config_setting_t *root, usbg_config *c)
{
	const char *func_label;
	usbg_function *target;
	int ret;

	func_label = config_setting_get_string(root);
	if (!func_label) {
		ret = USBG_ERROR_OTHER_ERROR;
		goto out;
	}

	target = usbg_lookup_function(c->parent, func_label);
	if (!target) {
		ret = USBG_ERROR_NOT_FOUND;
		goto out;
	}

	ret = usbg_add_config_function(c, target->name, target);
out:
	return ret;
}

static int usbg_import_binding_group(config_setting_t *root, usbg_config *c)
{
	config_setting_t *node;
	const char *func_label, *name;
	usbg_function *target;
	int ret;

	node = config_setting_get_member(root, USBG_FUNCTION_TAG);
	if (!node) {
		ret = USBG_ERROR_MISSING_TAG;
		goto out;
	}

	/* It is allowed to provide link to existing function
	 * or define unlabeled instance of function in this place */
	if (usbg_config_is_string(node)) {
		func_label = config_setting_get_string(node);
		if (!func_label) {
			ret = USBG_ERROR_OTHER_ERROR;
			goto out;
		}

		target = usbg_lookup_function(c->parent, func_label);
		if (!target) {
			ret = USBG_ERROR_NOT_FOUND;
			goto out;
		}
	} else if (config_setting_is_group(node)) {
		config_setting_t *inst_node;
		const char *instance;

		inst_node = config_setting_get_member(node, USBG_INSTANCE_TAG);
		if (!inst_node) {
			ret = USBG_ERROR_MISSING_TAG;
			goto out;
		}

		instance = config_setting_get_string(inst_node);
		if (!instance) {
			ret = USBG_ERROR_OTHER_ERROR;
			goto out;
		}

		ret = usbg_import_function_run(c->parent, node,
					       instance, &target);
		if (ret != USBG_SUCCESS)
			goto out;
	} else {
		ret = USBG_ERROR_INVALID_TYPE;
		goto out;
	}

	/* Name tag is optional. When no such tag, default one will be used */
	node = config_setting_get_member(root, USBG_NAME_TAG);
	if (node) {
		if (!usbg_config_is_string(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto out;
		}

		name = config_setting_get_string(node);
		if (!name) {
			ret = USBG_ERROR_OTHER_ERROR;
			goto out;
		}
	} else {
		name = target->name;
	}

	ret = usbg_add_config_function(c, name, target);
out:
	return ret;
}

static int usbg_import_config_bindings(config_setting_t *root, usbg_config *c)
{
	config_setting_t *node;
	int ret = USBG_SUCCESS;
	int count, i;

	count = config_setting_length(root);

	for (i = 0; i < count; ++i) {
		node = config_setting_get_elem(root, i);

		if (usbg_config_is_string(node))
			ret = usbg_import_binding_string(node, c);
		else if (config_setting_is_group(node))
			ret = usbg_import_binding_group(node, c);
		else
			ret = USBG_ERROR_INVALID_TYPE;

		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_import_config_strs_lang(config_setting_t *root, usbg_config *c)
{
	config_setting_t *node;
	int lang;
	const char *str;
	usbg_config_strs c_strs = {0};
	int ret = USBG_ERROR_INVALID_TYPE;

	node = config_setting_get_member(root, USBG_LANG_TAG);
	if (!node) {
		ret = USBG_ERROR_MISSING_TAG;
		goto out;
	}

	if (!usbg_config_is_int(node))
		goto out;

	lang = config_setting_get_int(node);

	/* Configuratin string is optional */
	node = config_setting_get_member(root, "configuration");
	if (node) {
		if (!usbg_config_is_string(node))
			goto out;

		str = config_setting_get_string(node);

		/* Auto truncate the string to max length */
		strncpy(c_strs.configuration, str, USBG_MAX_STR_LENGTH);
		c_strs.configuration[USBG_MAX_STR_LENGTH - 1] = 0;
	}

	ret = usbg_set_config_strs(c, lang, &c_strs);

out:
	return ret;
}

static int usbg_import_config_strings(config_setting_t *root, usbg_config *c)
{
	config_setting_t *node;
	int ret = USBG_SUCCESS;
	int count, i;

	count = config_setting_length(root);

	for (i = 0; i < count; ++i) {
		node = config_setting_get_elem(root, i);
		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		ret = usbg_import_config_strs_lang(node, c);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_import_config_attrs(config_setting_t *root, usbg_config *c)
{
	config_setting_t *node;
	int usbg_ret;
	int bmAttributes, bMaxPower;
	int ret = USBG_ERROR_INVALID_TYPE;

	node = config_setting_get_member(root, "bmAttributes");
	if (node) {
		if (!usbg_config_is_int(node))
			goto out;

		bmAttributes = config_setting_get_int(node);
		usbg_ret = usbg_set_config_bm_attrs(c, bmAttributes);
		if (usbg_ret != USBG_SUCCESS) {
			ret = usbg_ret;
			goto out;
		}
	}

	node = config_setting_get_member(root, "bMaxPower");
	if (node) {
		if (!usbg_config_is_int(node))
			goto out;

		bMaxPower = config_setting_get_int(node);
		usbg_ret = usbg_set_config_max_power(c, bMaxPower);
		if (usbg_ret != USBG_SUCCESS) {
			ret = usbg_ret;
			goto out;
		}
	}

	/* Empty attrs section is also considered to be valid */
	ret = USBG_SUCCESS;
out:
	return ret;

}

static int usbg_import_config_run(usbg_gadget *g, config_setting_t *root,
				  int id, usbg_config **c)
{
	config_setting_t *node;
	const char *name;
	usbg_config *newc;
	int usbg_ret;
	int ret = USBG_ERROR_MISSING_TAG;

	/*
	 * Label is mandatory,
	 * if attrs aren't present defaults are used
	 */
	node = config_setting_get_member(root, USBG_NAME_TAG);
	if (!node)
		goto out;

	name = config_setting_get_string(node);
	if (!name) {
		ret = USBG_ERROR_INVALID_TYPE;
		goto out;
	}

	/* Required data collected, let's create our config */
	usbg_ret = usbg_create_config(g, id, name, NULL, NULL, &newc);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	/* Attrs are optional */
	node = config_setting_get_member(root, USBG_ATTRS_TAG);
	if (node) {
		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}

		usbg_ret = usbg_import_config_attrs(node, newc);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	/* Strings are also optional */
	node = config_setting_get_member(root, USBG_STRINGS_TAG);
	if (node) {
		if (!config_setting_is_list(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}

		usbg_ret = usbg_import_config_strings(node, newc);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	/* Functions too, because some config may not be
	 * fully configured and not contain any function */
	node = config_setting_get_member(root, USBG_FUNCTIONS_TAG);
	if (node) {
		if (!config_setting_is_list(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}

		usbg_ret = usbg_import_config_bindings(node, newc);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	*c = newc;
	ret = USBG_SUCCESS;
out:
	return ret;

error:
	ret = usbg_ret;
error2:
	/* We ignore returned value, if function fails
	 * there is no way to handle it */
	usbg_rm_config(newc, USBG_RM_RECURSE);
	return ret;
}

static int usbg_import_gadget_configs(config_setting_t *root, usbg_gadget *g)
{
	config_setting_t *node, *id_node;
	int id;
	usbg_config *c;
	int ret = USBG_SUCCESS;
	int count, i;

	count = config_setting_length(root);

	for (i = 0; i < count; ++i) {
		node = config_setting_get_elem(root, i);
		if (!node) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		/* Look for id */
		id_node = config_setting_get_member(node, USBG_ID_TAG);
		if (!id_node) {
			ret = USBG_ERROR_MISSING_TAG;
			break;
		}

		if (!usbg_config_is_int(id_node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		id = config_setting_get_int(id_node);

		ret = usbg_import_config_run(g, node, id, &c);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}

static int usbg_import_gadget_functions(config_setting_t *root, usbg_gadget *g)
{
	config_setting_t *node, *inst_node;
	const char *instance;
	const char *label;
	usbg_function *f;
	int ret = USBG_SUCCESS;
	int count, i;

	count = config_setting_length(root);

	for (i = 0; i < count; ++i) {
		node = config_setting_get_elem(root, i);
		if (!node) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		/* Look for instance name */
		inst_node = config_setting_get_member(node, USBG_INSTANCE_TAG);
		if (!inst_node) {
			ret = USBG_ERROR_MISSING_TAG;
			break;
		}

		if (!usbg_config_is_string(inst_node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		instance = config_setting_get_string(inst_node);
		if (!instance) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		ret = usbg_import_function_run(g, node, instance, &f);
		if (ret != USBG_SUCCESS)
			break;

		/* Set the label given by user */
		label = config_setting_name(node);
		if (!label) {
			ret = USBG_ERROR_OTHER_ERROR;
			break;
		}

		f->label = strdup(label);
		if (!f->label) {
			ret = USBG_ERROR_NO_MEM;
			break;
		}
	}

	return ret;
}

static int usbg_import_gadget_strs_lang(config_setting_t *root, usbg_gadget *g)
{
	config_setting_t *node;
	int lang;
	const char *str;
	usbg_gadget_strs g_strs = {0};
	int ret = USBG_ERROR_INVALID_TYPE;

	node = config_setting_get_member(root, USBG_LANG_TAG);
	if (!node) {
		ret = USBG_ERROR_MISSING_TAG;
		goto out;
	}

	if (!usbg_config_is_int(node))
		goto out;

	lang = config_setting_get_int(node);

	/* Auto truncate the string to max length */
#define GET_OPTIONAL_GADGET_STR(NAME, FIELD)				\
	do {								\
		node = config_setting_get_member(root, #NAME);		\
		if (node) {						\
			if (!usbg_config_is_string(node))		\
				goto out;				\
			str = config_setting_get_string(node);		\
			strncpy(g_strs.FIELD, str, USBG_MAX_STR_LENGTH); \
			g_strs.FIELD[USBG_MAX_STR_LENGTH - 1] = '\0';	\
		}							\
	} while (0)

	GET_OPTIONAL_GADGET_STR(manufacturer, str_mnf);
	GET_OPTIONAL_GADGET_STR(product, str_prd);
	GET_OPTIONAL_GADGET_STR(serialnumber, str_ser);

#undef GET_OPTIONAL_GADGET_STR

	ret = usbg_set_gadget_strs(g, lang, &g_strs);

out:
	return ret;
}

static int usbg_import_gadget_strings(config_setting_t *root, usbg_gadget *g)
{
	config_setting_t *node;
	int ret = USBG_SUCCESS;
	int count, i;

	count = config_setting_length(root);

	for (i = 0; i < count; ++i) {
		node = config_setting_get_elem(root, i);
		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			break;
		}

		ret = usbg_import_gadget_strs_lang(node, g);
		if (ret != USBG_SUCCESS)
			break;
	}

	return ret;
}


static int usbg_import_gadget_attrs(config_setting_t *root, usbg_gadget *g)
{
	config_setting_t *node;
	int usbg_ret;
	int val;
	int ret = USBG_ERROR_INVALID_TYPE;

#define GET_OPTIONAL_GADGET_ATTR(NAME, FUNC_END, TYPE)			\
	do {								\
		node = config_setting_get_member(root, #NAME);		\
		if (node) {						\
			if (!usbg_config_is_int(node))			\
				goto out;				\
			val = config_setting_get_int(node);		\
			if (val < 0 || val > ((1L << (sizeof(TYPE)*8)) - 1)) { \
				ret = USBG_ERROR_INVALID_VALUE;		\
				goto out;				\
			}						\
			usbg_ret = usbg_set_gadget_##FUNC_END(g, (TYPE)val); \
			if (usbg_ret != USBG_SUCCESS) {			\
				ret = usbg_ret;				\
				goto out;				\
			}						\
		}							\
	} while (0)

	GET_OPTIONAL_GADGET_ATTR(bcdUSB, device_bcd_usb, uint16_t);
	GET_OPTIONAL_GADGET_ATTR(bDeviceClass, device_class, uint8_t);
	GET_OPTIONAL_GADGET_ATTR(bDeviceSubClass, device_subclass, uint8_t);
	GET_OPTIONAL_GADGET_ATTR(bDeviceProtocol, device_protocol, uint8_t);
	GET_OPTIONAL_GADGET_ATTR(bMaxPacketSize0, device_max_packet, uint8_t);
	GET_OPTIONAL_GADGET_ATTR(idVendor, vendor_id, uint16_t);
	GET_OPTIONAL_GADGET_ATTR(idProduct, product_id, uint16_t);
	GET_OPTIONAL_GADGET_ATTR(bcdDevice, device_bcd_device, uint16_t);

#undef GET_OPTIONAL_GADGET_ATTR

	/* Empty attrs section is also considered to be valid */
	ret = USBG_SUCCESS;
out:
	return ret;

}

static int usbg_import_gadget_run(usbg_state *s, config_setting_t *root,
				  const char *name, usbg_gadget **g)
{
	config_setting_t *node;
	usbg_gadget *newg;
	int usbg_ret;
	int ret = USBG_ERROR_MISSING_TAG;

	/* There is no mandatory data in gadget so let's start with
	 * creating a new gadget */
	usbg_ret = usbg_create_gadget(s, name, NULL, NULL, &newg);
	if (usbg_ret != USBG_SUCCESS) {
		ret = usbg_ret;
		goto out;
	}

	/* Attrs are optional */
	node = config_setting_get_member(root, USBG_ATTRS_TAG);
	if (node) {
		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}

		usbg_ret = usbg_import_gadget_attrs(node, newg);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	/* Strings are also optional */
	node = config_setting_get_member(root, USBG_STRINGS_TAG);
	if (node) {
		if (!config_setting_is_list(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}

		usbg_ret = usbg_import_gadget_strings(node, newg);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	/* Functions too, because some gadgets may not be fully
	* configured and don't have any funciton or have all functions
	* defined inline in configurations */
	node = config_setting_get_member(root, USBG_FUNCTIONS_TAG);
	if (node) {
		if (!config_setting_is_group(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}
		usbg_ret = usbg_import_gadget_functions(node, newg);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	/* Some gadget may not be fully configured
	 * so configs are also optional */
	node = config_setting_get_member(root, USBG_CONFIGS_TAG);
	if (node) {
		if (!config_setting_is_list(node)) {
			ret = USBG_ERROR_INVALID_TYPE;
			goto error2;
		}
		usbg_ret = usbg_import_gadget_configs(node, newg);
		if (usbg_ret != USBG_SUCCESS)
			goto error;
	}

	*g = newg;
	ret = USBG_SUCCESS;
out:
	return ret;

error:
	ret = usbg_ret;
error2:
	/* We ignore returned value, if function fails
	 * there is no way to handle it */
	usbg_rm_gadget(newg, USBG_RM_RECURSE);
	return ret;
}

int usbg_import_function(usbg_gadget *g, FILE *stream, const char *instance,
			 usbg_function **f)
{
	config_t *cfg;
	config_setting_t *root;
	usbg_function *newf;
	int ret, cfg_ret;

	if (!g || !stream || !instance)
		return USBG_ERROR_INVALID_PARAM;

	cfg = malloc(sizeof(*cfg));
	if (!cfg)
		return USBG_ERROR_NO_MEM;

	config_init(cfg);

	cfg_ret = config_read(cfg, stream);
	if (cfg_ret != CONFIG_TRUE) {
		usbg_set_failed_import(&g->last_failed_import, cfg);
		ret = USBG_ERROR_INVALID_FORMAT;
		goto out;
	}

	/* Allways successful */
	root = config_root_setting(cfg);

	ret = usbg_import_function_run(g, root, instance, &newf);
	if (ret != USBG_SUCCESS) {
		usbg_set_failed_import(&g->last_failed_import, cfg);
		goto out;
	}

	if (f)
		*f = newf;

	config_destroy(cfg);
	free(cfg);
	/* Clean last error */
	usbg_set_failed_import(&g->last_failed_import, NULL);
out:
	return ret;

}

int usbg_import_config(usbg_gadget *g, FILE *stream, int id,  usbg_config **c)
{
	config_t *cfg;
	config_setting_t *root;
	usbg_config *newc;
	int ret, cfg_ret;

	if (!g || !stream || id < 0)
		return USBG_ERROR_INVALID_PARAM;

	cfg = malloc(sizeof(*cfg));
	if (!cfg)
		return USBG_ERROR_NO_MEM;

	config_init(cfg);

	cfg_ret = config_read(cfg, stream);
	if (cfg_ret != CONFIG_TRUE) {
		usbg_set_failed_import(&g->last_failed_import, cfg);
		ret = USBG_ERROR_INVALID_FORMAT;
		goto out;
	}

	/* Allways successful */
	root = config_root_setting(cfg);

	ret = usbg_import_config_run(g, root, id, &newc);
	if (ret != USBG_SUCCESS) {
		usbg_set_failed_import(&g->last_failed_import, cfg);
		goto out;
	}

	if (c)
		*c = newc;

	config_destroy(cfg);
	free(cfg);
	/* Clean last error */
	usbg_set_failed_import(&g->last_failed_import, NULL);
out:
	return ret;
}

int usbg_import_gadget(usbg_state *s, FILE *stream, const char *name,
		       usbg_gadget **g)
{
	config_t *cfg;
	config_setting_t *root;
	usbg_gadget *newg;
	int ret, cfg_ret;

	if (!s || !stream || !name)
		return USBG_ERROR_INVALID_PARAM;

	cfg = malloc(sizeof(*cfg));
	if (!cfg)
		return USBG_ERROR_NO_MEM;

	config_init(cfg);

	cfg_ret = config_read(cfg, stream);
	if (cfg_ret != CONFIG_TRUE) {
		usbg_set_failed_import(&s->last_failed_import, cfg);
		ret = USBG_ERROR_INVALID_FORMAT;
		goto out;
	}

	/* Allways successful */
	root = config_root_setting(cfg);

	ret = usbg_import_gadget_run(s, root, name, &newg);
	if (ret != USBG_SUCCESS) {
		usbg_set_failed_import(&s->last_failed_import, cfg);
		goto out;
	}

	if (g)
		*g = newg;

	config_destroy(cfg);
	free(cfg);
	/* Clean last error */
	usbg_set_failed_import(&s->last_failed_import, NULL);
out:
	return ret;
}

const char *usbg_get_func_import_error_text(usbg_gadget *g)
{
	if (!g || !g->last_failed_import)
		return NULL;

	return config_error_text(g->last_failed_import);
}

int usbg_get_func_import_error_line(usbg_gadget *g)
{
	if (!g || !g->last_failed_import)
		return -1;

	return config_error_line(g->last_failed_import);
}

const char *usbg_get_config_import_error_text(usbg_gadget *g)
{
	if (!g || !g->last_failed_import)
		return NULL;

	return config_error_text(g->last_failed_import);
}

int usbg_get_config_import_error_line(usbg_gadget *g)
{
	if (!g || !g->last_failed_import)
		return -1;

	return config_error_line(g->last_failed_import);
}

const char *usbg_get_gadget_import_error_text(usbg_state *s)
{
	if (!s || !s->last_failed_import)
		return NULL;

	return config_error_text(s->last_failed_import);
}

int usbg_get_gadget_import_error_line(usbg_state *s)
{
	if (!s || !s->last_failed_import)
		return -1;

	return config_error_line(s->last_failed_import);
}

