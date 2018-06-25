/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <efivar.h>
#include <efivar/efiboot.h>

#include "fu-ucs2.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device-info.h"
#include "fu-uefi-vars.h"

static gchar *
fu_uefi_device_info_build_varname (const gchar *guidstr, guint64 hw_inst)
{
	return g_strdup_printf ("fwupdate-%s-%"G_GUINT64_FORMAT, guidstr, hw_inst);
}

gboolean
fu_uefi_device_info_update (FuUefiDeviceInfo *info, GError **error)
{
	const guint64 hw_inst = 0;
	efi_guid_t guid;
	gssize dp_sz;
	gssize info2_sz;
	g_autofree FuUefiDeviceInfo *info2 = NULL;
	g_autofree gchar *guidstr = NULL;
	g_autofree gchar *varname = NULL;

	g_return_val_if_fail (info != NULL, FALSE);

	/* key name */
	memcpy (&guid, &info->guid, sizeof(guid));
	if (efi_guid_to_str (&guid, &guidstr) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get convert GUID");
		return FALSE;
	}

	/* make sure dp_sz is at least big enough to have our structure: FIXME? really? */
	dp_sz = efidp_size ((efidp)info->dp_ptr);
	if (dp_sz < 0 || (gsize) dp_sz < sizeof(FuUefiDeviceInfo)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "EFI DP size %" G_GSSIZE_FORMAT " impossible",
			     dp_sz);
		return FALSE;
	}

	/* make sure sizeof(*info) + dp_sz won't integer overflow */
	if (((gsize)dp_sz >= SSIZE_MAX - sizeof(FuUefiDeviceInfo)) ||
	    ((gssize)dp_sz > sysconf (_SC_PAGESIZE) * 100)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "device path size (%zd) would overflow", dp_sz);
		return FALSE;
	}

	/* create new info and save to EFI vars */
	info2_sz = sizeof(FuUefiDeviceInfo) + dp_sz - sizeof(info->dp_ptr);
	info2 = g_malloc0 (info2_sz);
	memcpy (info2, info, sizeof(FuUefiDeviceInfo));
	memcpy (info2->dp_ptr, info->dp_ptr, dp_sz);
	varname = fu_uefi_device_info_build_varname (guidstr, hw_inst);
	return fu_uefi_vars_set_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				      (guint8 *) info2, info2_sz,
				      FU_UEFI_VARS_ATTR_NON_VOLATILE |
				      FU_UEFI_VARS_ATTR_BOOTSERVICE_ACCESS |
				      FU_UEFI_VARS_ATTR_RUNTIME_ACCESS,
				      error);
}

FuUefiDeviceInfo *
fu_uefi_device_info_new (const gchar *guidstr, guint64 hw_inst, GError **error)
{
	FuUefiDeviceInfo *info;
	efidp_header *dp;
	gsize data_size = 0;
	gssize sz;
	g_autofree gchar *varname = NULL;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail (guidstr != NULL, NULL);

	varname = fu_uefi_device_info_build_varname (guidstr, hw_inst);
	if (!fu_uefi_vars_get_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				    &data, &data_size, NULL, NULL)) {
		efi_guid_t guid;

		/* convert to packed version */
		if (efi_str_to_guid (guidstr, &guid) < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to convert %s",
				     guidstr);
			return NULL;
		}

		/* create a new struct */
		info = g_new0 (FuUefiDeviceInfo, 1);
		info->update_info_version = UPDATE_INFO_VERSION;
		info->guid = guid;
		info->hw_inst = hw_inst;
		info->dp_ptr = g_malloc0 (1024);
		sz = efidp_make_end_entire ((guint8 *)info->dp_ptr, 1024);
		if (sz < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get pad DP data %s",
				     varname);
			return NULL;
		}
		return info;
	}

	/* if our size is wrong, or our data is otherwise bad, try to delete
	 * the variable and create a new one. */
	if (data_size < sizeof (FuUefiDeviceInfo) || data == NULL) {
		g_debug ("uefi saved state size mismatch");
		if (!fu_uefi_vars_delete (FU_UEFI_VARS_GUID_FWUPDATE, varname, error)) {
			g_prefix_error (error, "failed to delete EFI variable %s", varname);
			return NULL;
		}
		return fu_uefi_device_info_new (guidstr, hw_inst, error);
	}

	/* check the data is of the correct format */
	info = (FuUefiDeviceInfo *)data;
	if (info->update_info_version != UPDATE_INFO_VERSION) {
		g_debug ("uefi saved state version mismatch");
		if (!fu_uefi_vars_delete (FU_UEFI_VARS_GUID_FWUPDATE, varname, error)) {
			g_prefix_error (error, "failed to delete EFI variable %s", varname);
			return NULL;
		}
		return fu_uefi_device_info_new (guidstr, hw_inst, error);
	}

	/* reallocate the buffer to a efidp_header */
	sz = efidp_size ((efidp)info->dp_buf);
	if (sz < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get DP size of EFI variable %s",
			     varname);
		return NULL;
	}
	dp = g_malloc0 ((gsize) sz);
	memcpy (dp, info->dp_buf, (gsize) sz);
	info->dp_ptr = dp;
	return info;
}

void
fu_uefi_device_info_free (FuUefiDeviceInfo *info)
{
	if (info == NULL)
		return;
	g_free (info->dp_ptr);
	g_free (info);
}

gboolean
fu_uefi_device_info_set_capsule_fn (FuUefiDeviceInfo *info,
				    const gchar *path, GError **error)
{
	gssize req;
	gssize sz;
	g_autofree guint8 *dp_buf = NULL;

	/* get the size of the path first */
	req = efi_generate_file_device_path (NULL, 0, path,
					     EFIBOOT_OPTIONS_IGNORE_FS_ERROR |
					     EFIBOOT_ABBREV_HD);
	if (req < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to efi_generate_file_device_path(%s)",
			     path);
		return FALSE;
	}
	if (req <= 4) { /* if we just have an end device path,
			  it's not going to work. */
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get valid device_path for (%s)",
			     path);
		return FALSE;
	}

	/* actually get the path this time */
	dp_buf = g_malloc0 (req);
	sz = efi_generate_file_device_path (dp_buf, req, path,
					    EFIBOOT_OPTIONS_IGNORE_FS_ERROR |
					    EFIBOOT_ABBREV_HD);
	if (sz < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to efi_generate_file_device_path(%s)",
			     path);
		return FALSE;
	}

	/* info owns this now */
	if (info->dp_ptr != NULL)
		g_free (info->dp_ptr);
	info->dp_ptr = (efidp_header *) g_steal_pointer (&dp_buf);
	return TRUE;
}

static gboolean
efidp_end_entire (efidp_header *dp)
{
	if (dp == NULL)
		return FALSE;
	if (efidp_type ((efidp)dp) != EFIDP_END_TYPE)
		return FALSE;
	if (efidp_subtype ((efidp)dp) != EFIDP_END_ENTIRE)
		return FALSE;
	return TRUE;
}

static gchar *
fu_uefi_device_info_get_existing_capsule_fn (FuUefiDeviceInfo *info)
{
	const_efidp idp;
	gint rc;
	guint16 ucs2sz = 0;
	g_autofree gchar *relpath = NULL;
	g_autofree guint16 *ucs2file = NULL;

	g_return_val_if_fail (info != NULL, NULL);

	/* never set */
	if (info->dp_ptr == NULL)
		return NULL;
	if (efidp_end_entire (info->dp_ptr))
		return NULL;

	/* find UCS2 string */
	idp = (const_efidp) info->dp_ptr;
	while (1) {
		if (efidp_type (idp) == EFIDP_END_TYPE &&
		    efidp_subtype (idp) == EFIDP_END_ENTIRE)
			break;
		if (efidp_type(idp) != EFIDP_MEDIA_TYPE ||
		    efidp_subtype (idp) != EFIDP_MEDIA_FILE) {
			rc = efidp_next_node (idp, &idp);
			if (rc < 0)
				break;
			continue;
		}
		ucs2sz = efidp_node_size (idp) - 4;
		ucs2file = g_new0 (guint16, (ucs2sz / 2) + 1);
		memcpy (ucs2file, (guint8 *)idp + 4, ucs2sz);
		break;
	}

	/* nothing found */
	if (ucs2file == NULL || ucs2sz <= 0)
		return NULL;

	/* convert to something sane */
	relpath = fu_ucs2_to_uft8 (ucs2file, ucs2sz / sizeof (guint16));
	if (relpath == NULL)
		return NULL;
	g_strdelimit (relpath, "\\", '/');
	return g_steal_pointer (&relpath);
}

gchar *
fu_uefi_device_info_get_capsule_fn (FuUefiDeviceInfo *info, const gchar *esp_path)
{
	efi_guid_t guid;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *guidstr = NULL;
	g_autofree gchar *media_path = NULL;

	/* we've updated this GUID before */
	media_path = fu_uefi_device_info_get_existing_capsule_fn (info);
	if (media_path != NULL)
		return g_build_filename (esp_path, media_path, NULL);

	/* use the default fw path using the GUID in the name */
	memcpy (&guid, &info->guid, sizeof(guid));
	efi_guid_to_str (&guid, &guidstr);
	directory = fu_uefi_get_esp_path_for_os (esp_path);
	basename = g_strdup_printf ("fwupdate-%s.cap", guidstr);
	return g_build_filename (directory, "fw", basename, NULL);
}
