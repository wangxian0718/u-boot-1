/*
 *  Copyright (C) 2010-2016 NVIDIA CORPORATION.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <stdlib.h>
#include <common.h>
#include <fdt_support.h>
#include <fdtdec.h>
#include "dt-edit.h"

#define fdt_for_each_property(fdt, prop, parent)		\
	for (prop = fdt_first_property_offset(fdt, parent);	\
	     prop >= 0;						\
	     prop = fdt_next_property_offset(fdt, prop))

static int fdt_copy_node_content(void *blob_src, int ofs_src, void *blob_dst,
		int ofs_dst, int indent)
{
	int ofs_src_child, ofs_dst_child;

	/*
	 * FIXME: This doesn't remove properties or nodes in the destination
	 * that are not present in the source. For the nodes we care about
	 * right now, this is not an issue.
	 */

	fdt_for_each_property(blob_src, ofs_src_child, ofs_src) {
		const void *prop;
		const char *name;
		int len, ret;

		prop = fdt_getprop_by_offset(blob_src, ofs_src_child, &name,
					     &len);
		debug("%s: %*scopy prop: %s\n", __func__, indent, "", name);

		ret = fdt_setprop(blob_dst, ofs_dst, name, prop, len);
		if (ret < 0) {
			error("Can't copy DT prop %s\n", name);
			return ret;
		}
	}

	fdt_for_each_subnode(blob_src, ofs_src_child, ofs_src) {
		const char *name;

		name = fdt_get_name(blob_src, ofs_src_child, NULL);
		debug("%s: %*scopy node: %s\n", __func__, indent, "", name);

		ofs_dst_child = fdt_subnode_offset(blob_dst, ofs_dst, name);
		if (ofs_dst_child < 0) {
			debug("%s: %*s(creating it in dst)\n", __func__,
			      indent, "");
			ofs_dst_child = fdt_add_subnode(blob_dst, ofs_dst,
							name);
			if (ofs_dst_child < 0) {
				error("Can't copy DT node %s\n", name);
				return ofs_dst_child;
			}
		}

		fdt_copy_node_content(blob_src, ofs_src_child, blob_dst,
				      ofs_dst_child, indent + 2);
	}

	return 0;
}

static int fdt_add_path(void *blob, const char *path)
{
	char *pcopy, *tmp, *node;
	int ofs_parent, ofs_child, ret;

	if (path[0] != '/') {
		error("Can't add path %s; missing leading /", path);
		return -1;
	}
	path++;
	if (!*path) {
		debug("%s: path points at DT root!", __func__);
		return 0;
	}

	pcopy = strdup(path);
	if (!pcopy) {
		error("strdup() failed");
		return -1;
	}

	tmp = pcopy;
	ofs_parent = 0;
	while (true) {
		node = strsep(&tmp, "/");
		if (!node)
			break;
		debug("%s: node=%s\n", __func__, node);
		ofs_child = fdt_subnode_offset(blob, ofs_parent, node);
		if (ofs_child < 0)
			ofs_child = fdt_add_subnode(blob, ofs_parent, node);
		if (ofs_child < 0) {
			error("Can't create DT node %s\n", node);
			ret = ofs_child;
			goto out;
		}
		ofs_parent = ofs_child;
	}
	ret = ofs_parent;

out:
	free(pcopy);

	return ret;
}

__weak void *fdt_copy_get_blob_src_default(void)
{
	return NULL;
}

static void *fdt_get_copy_blob_src(void)
{
	char *src_addr_s;

	src_addr_s = getenv("fdt_copy_src_addr");
	if (!src_addr_s)
		return fdt_copy_get_blob_src_default();
	return (void *)simple_strtoul(src_addr_s, NULL, 16);
}

static int fdt_copy_prop(void *blob_src, void *blob_dst, char *prop_path)
{
	char *prop_name;
	const void *prop;
	int ofs_src, ofs_dst, len, ret;

	prop_name = strrchr(prop_path, '/');
	if (!prop_name) {
		error("Can't copy prop %s; missing /", prop_path);
		return -1;
	}
	*prop_name = 0;
	prop_name++;

	if (*prop_path) {
		ofs_src = fdt_path_offset(blob_src, prop_path);
		if (ofs_src < 0) {
			error("DT node %s missing in source; can't copy %s\n",
			      prop_path, prop_name);
			return -1;
		}

		ofs_dst = fdt_path_offset(blob_dst, prop_path);
		if (ofs_src < 0) {
			error("DT node %s missing in dest; can't copy prop %s\n",
			      prop_path, prop_name);
			return -1;
		}
	} else {
		ofs_src = 0;
		ofs_dst = 0;
	}

	prop = fdt_getprop(blob_src, ofs_src, prop_name, &len);
	if (!prop) {
		error("DT property %s/%s missing in source; can't copy\n",
		      prop_path, prop_name);
		return -1;
	}

	ret = fdt_setprop(blob_dst, ofs_dst, prop_name, prop, len);
	if (ret < 0) {
		error("Can't set DT prop %s/%s\n", prop_path, prop_name);
		return ret;
	}

	return 0;
}

int fdt_copy_env_proplist(void *blob_dst)
{
	void *blob_src;
	char *prop_paths, *tmp, *prop_path;
	int ret;

	blob_src = fdt_get_copy_blob_src();
	if (!blob_src) {
		debug("%s: No source DT\n", __func__);
		return 0;
	}

	prop_paths = getenv("fdt_copy_prop_paths");
	if (!prop_paths) {
		debug("%s: No env var\n", __func__);
		return 0;
	}

	prop_paths = strdup(prop_paths);
	if (!prop_paths) {
		error("strdup() failed");
		return -1;
	}

	tmp = prop_paths;
	while (true) {
		prop_path = strsep(&tmp, ":");
		if (!prop_path)
			break;
		debug("%s: prop to copy: %s\n", __func__, prop_path);
		ret = fdt_copy_prop(blob_src, blob_dst, prop_path);
		if (ret < 0) {
			ret = -1;
			goto out;
		}
	}

	ret = 0;

out:
	free(prop_paths);

	return ret;
}

static int fdt_copy_node(void *blob_src, void *blob_dst, char *path)
{
	int ofs_dst, ofs_src;
	int ret;

	ofs_dst = fdt_add_path(blob_dst, path);
	if (ofs_dst < 0) {
		error("Can't find/create dest DT node %s to copy\n", path);
		return ofs_dst;
	}

	if (!fdtdec_get_is_enabled(blob_dst, ofs_dst)) {
		debug("%s: DT node %s disabled in dest; skipping copy\n",
			__func__, path);
		return 0;
	}

	ofs_src = fdt_path_offset(blob_src, path);
	if (ofs_src < 0) {
		error("DT node %s missing in source; can't copy\n", path);
		return 0;
	}

	ret = fdt_copy_node_content(blob_src, ofs_src, blob_dst,
				    ofs_dst, 2);
	if (ret < 0)
		return ret;

	return 0;
}

int fdt_copy_env_nodelist(void *blob_dst)
{
	void *blob_src;
	char *node_paths, *tmp, *node_path;
	int ret;

	blob_src = fdt_get_copy_blob_src();
	if (!blob_src) {
		debug("%s: No source DT\n", __func__);
		return 0;
	}

	node_paths = getenv("fdt_copy_node_paths");
	if (!node_paths) {
		debug("%s: No env var\n", __func__);
		return 0;
	}

	node_paths = strdup(node_paths);
	if (!node_paths) {
		error("strdup() failed");
		return -1;
	}

	tmp = node_paths;
	while (true) {
		node_path = strsep(&tmp, ":");
		if (!node_path)
			break;
		debug("%s: node to copy: %s\n", __func__, node_path);
		ret = fdt_copy_node(blob_src, blob_dst, node_path);
		if (ret < 0) {
			ret = -1;
			goto out;
		}
	}

	ret = 0;

out:
	free(node_paths);

	return ret;
}
