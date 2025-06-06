/** @file moal_uap_cfg80211.c
 *
 * @brief This file contains the functions for uAP CFG80211.
 *
 *
 * Copyright 2011-2024 NXP
 *
 * This software file (the File) is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the License).  You may use, redistribute and/or modify the File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include "moal_cfg80211.h"
#include "moal_uap_cfg80211.h"
/** deauth reason code */
#define REASON_CODE_DEAUTH_LEAVING 3
/********************************************************
				Local Variables
********************************************************/

/********************************************************
				Global Variables
********************************************************/
extern const struct net_device_ops woal_uap_netdev_ops;
/********************************************************
				Local Functions
********************************************************/

/********************************************************
				Global Functions
********************************************************/
/**
 * @brief send deauth to station
 *
 * @param                 A pointer to moal_private
 * @param mac			  A pointer to station mac address
 * @param reason_code     ieee deauth reason code
 * @return                0 -- success, otherwise fail
 */
static int woal_deauth_station(moal_private *priv, u8 *mac_addr,
			       u16 reason_code)
{
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_bss *bss = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	bss = (mlan_ds_bss *)ioctl_req->pbuf;
	bss->sub_command = MLAN_OID_UAP_DEAUTH_STA;
	ioctl_req->req_id = MLAN_IOCTL_BSS;
	ioctl_req->action = MLAN_ACT_SET;

	moal_memcpy_ext(priv->phandle, bss->param.deauth_param.mac_addr,
			mac_addr, MLAN_MAC_ADDR_LENGTH,
			sizeof(bss->param.deauth_param.mac_addr));
	bss->param.deauth_param.reason_code = reason_code;
	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 * @brief send deauth to station, that has been added and associated
 *
 * @param                 A pointer to moal_private
 * @param mac			        A pointer to station mac address
 * @param reason_code     ieee deauth reason code
 * @return                0 -- success, otherwise fail
 */
static int woal_deauth_assoc_station(moal_private *priv, u8 *mac_addr,
				     u16 reason_code)
{
	int ret = 0;
	int i = 0;
	mlan_ds_get_info *info = NULL;
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!mac_addr) {
		LEAVE();
		return -EINVAL;
	}
#if KERNEL_VERSION(3, 8, 0) <= CFG80211_VERSION_CODE
	if (moal_extflg_isset(priv->phandle, EXT_HOST_MLME))
		cfg80211_del_sta(priv->netdev, mac_addr, GFP_KERNEL);
#endif
	if (priv->media_connected == MFALSE) {
		PRINTM(MINFO, "cfg80211: Media not connected!\n");
		LEAVE();
		return 0;
	}
	ioctl_req = (mlan_ioctl_req *)woal_alloc_mlan_ioctl_req(
		sizeof(mlan_ds_get_info) +
		(MAX_STA_LIST_IE_SIZE * MAX_NUM_CLIENTS));
	if (ioctl_req == NULL) {
		LEAVE();
		return -ENOMEM;
	}

	info = (mlan_ds_get_info *)ioctl_req->pbuf;
	info->sub_command = MLAN_OID_UAP_STA_LIST;
	ioctl_req->req_id = MLAN_IOCTL_GET_INFO;
	ioctl_req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!info->param.sta_list.sta_count) {
		PRINTM(MCMND, "wlan: skip deauth to station " MACSTR "\n",
		       MAC2STR(mac_addr));
		goto done;
	}

	for (i = 0; i < info->param.sta_list.sta_count; i++) {
		if (!memcmp(info->param.sta_list.info[i].mac_address, mac_addr,
			    ETH_ALEN)) {
			PRINTM(MMSG, "wlan: deauth station " MACSTR "\n",
			       MAC2STR(mac_addr));
			ret = woal_deauth_station(priv, mac_addr, reason_code);
			break;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);

	LEAVE();
	return ret;
}

/**
 * @brief send deauth to all station
 *
 * @param                 A pointer to moal_private
 * @param mac			  A pointer to station mac address
 *
 * @return                0 -- success, otherwise fail
 */
static int woal_deauth_all_station(moal_private *priv)
{
	int ret = -EFAULT;
	int i = 0;
	mlan_ds_get_info *info = NULL;
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	if (priv->media_connected == MFALSE) {
		PRINTM(MINFO, "cfg80211: Media not connected!\n");
		LEAVE();
		return 0;
	}
	PRINTM(MIOCTL, "del all station\n");
	/* Allocate an IOCTL request buffer */
	ioctl_req = (mlan_ioctl_req *)woal_alloc_mlan_ioctl_req(
		sizeof(mlan_ds_get_info) +
		(MAX_STA_LIST_IE_SIZE * MAX_NUM_CLIENTS));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	info = (mlan_ds_get_info *)ioctl_req->pbuf;
	info->sub_command = MLAN_OID_UAP_STA_LIST;
	ioctl_req->req_id = MLAN_IOCTL_GET_INFO;
	ioctl_req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS)
		goto done;
	if (!info->param.sta_list.sta_count)
		goto done;
	for (i = 0; i < info->param.sta_list.sta_count; i++) {
		PRINTM(MIOCTL, "deauth station " MACSTR "\n",
		       MAC2STR(info->param.sta_list.info[i].mac_address));
		ret = woal_deauth_station(
			priv, info->param.sta_list.info[i].mac_address,
			REASON_CODE_DEAUTH_LEAVING);
	}
	woal_sched_timeout(200);
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	return ret;
}

/**
 * @brief Verify RSN IE
 *
 * @param rsn_ie          Pointer RSN IE
 * @param sys_config      Pointer to mlan_uap_bss_param structure
 *
 * @return                MTRUE/MFALSE
 */
static t_u8 woal_check_rsn_ie(IEEEtypes_Rsn_t *rsn_ie,
			      mlan_uap_bss_param *sys_config)
{
	int left = 0;
	int count = 0;
	int i = 0;
	wpa_suite_auth_key_mgmt_t *key_mgmt = NULL;
	left = rsn_ie->len + 2;
	if (left < (int)sizeof(IEEEtypes_Rsn_t))
		return MFALSE;
	sys_config->wpa_cfg.group_cipher = 0;
	sys_config->wpa_cfg.pairwise_cipher_wpa2 = 0;
	sys_config->key_mgmt = 0;
	/* check the group cipher */
	switch (rsn_ie->group_cipher.type) {
	case WPA_CIPHER_TKIP:
		sys_config->wpa_cfg.group_cipher = CIPHER_TKIP;
		break;
	case WPA_CIPHER_AES_CCM:
		sys_config->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
		break;
	default:
		break;
	}
	count = woal_le16_to_cpu(rsn_ie->pairwise_cipher.count);
	if (count < 1)
		return MFALSE;

	for (i = 0; i < count; i++) {
		switch (rsn_ie->pairwise_cipher.list[i].type) {
		case WPA_CIPHER_TKIP:
			sys_config->wpa_cfg.pairwise_cipher_wpa2 |= CIPHER_TKIP;
			break;
		case WPA_CIPHER_AES_CCM:
			sys_config->wpa_cfg.pairwise_cipher_wpa2 |=
				CIPHER_AES_CCMP;
			break;
		default:
			break;
		}
	}
	left -= sizeof(IEEEtypes_Rsn_t) + (count) * sizeof(wpa_suite);
	if (left <
	    ((int)sizeof(wpa_suite_auth_key_mgmt_t) + (int)sizeof(wpa_suite)))
		return MFALSE;
	key_mgmt = (wpa_suite_auth_key_mgmt_t *)((u8 *)rsn_ie +
						 sizeof(IEEEtypes_Rsn_t) +
						 (count) * sizeof(wpa_suite));
	count = woal_le16_to_cpu(key_mgmt->count);
	if (left < ((int)sizeof(wpa_suite_auth_key_mgmt_t) +
		    (count) * (int)sizeof(wpa_suite)))
		return MFALSE;
	for (i = 0; i < count; i++) {
		switch (key_mgmt->list[i].type) {
		case RSN_AKM_8021X:
			sys_config->key_mgmt |= KEY_MGMT_EAP;
			break;
		case RSN_AKM_PSK:
			sys_config->key_mgmt |= KEY_MGMT_PSK;
			break;
		case RSN_AKM_PSK_SHA256:
			sys_config->key_mgmt |= KEY_MGMT_PSK_SHA256;
			break;
		case RSN_AKM_SAE:
			sys_config->key_mgmt |= KEY_MGMT_SAE;
			break;
		case RSN_AKM_OWE:
			sys_config->key_mgmt |= KEY_MGMT_OWE;
			break;
		}
	}
	return MTRUE;
}

/**
 * @brief Verify WPA IE
 *
 * @param wpa_ie          Pointer WPA IE
 * @param sys_config      Pointer to mlan_uap_bss_param structure
 *
 * @return                MTRUE/MFALSE
 */
static t_u8 woal_check_wpa_ie(IEEEtypes_Wpa_t *wpa_ie,
			      mlan_uap_bss_param *sys_config)
{
	int left = 0;
	int count = 0;
	int i = 0;
	wpa_suite_auth_key_mgmt_t *key_mgmt = NULL;
	left = wpa_ie->len + 2;
	if (left < (int)sizeof(IEEEtypes_Wpa_t))
		return MFALSE;
	sys_config->wpa_cfg.group_cipher = 0;
	sys_config->wpa_cfg.pairwise_cipher_wpa = 0;
	switch (wpa_ie->group_cipher.type) {
	case WPA_CIPHER_TKIP:
		sys_config->wpa_cfg.group_cipher = CIPHER_TKIP;
		break;
	case WPA_CIPHER_AES_CCM:
		sys_config->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
		break;
	default:
		break;
	}
	count = woal_le16_to_cpu(wpa_ie->pairwise_cipher.count);
	if (count < 1)
		return MFALSE;

	for (i = 0; i < count; i++) {
		switch (wpa_ie->pairwise_cipher.list[i].type) {
		case WPA_CIPHER_TKIP:
			sys_config->wpa_cfg.pairwise_cipher_wpa |= CIPHER_TKIP;
			break;
		case WPA_CIPHER_AES_CCM:
			sys_config->wpa_cfg.pairwise_cipher_wpa |=
				CIPHER_AES_CCMP;
			break;
		default:
			break;
		}
	}
	left -= sizeof(IEEEtypes_Wpa_t) + (count) * sizeof(wpa_suite);
	if (left <
	    ((int)sizeof(wpa_suite_auth_key_mgmt_t) + (int)sizeof(wpa_suite)))
		return MFALSE;
	key_mgmt = (wpa_suite_auth_key_mgmt_t *)((u8 *)wpa_ie +
						 sizeof(IEEEtypes_Wpa_t) +
						 (count) * sizeof(wpa_suite));
	count = woal_le16_to_cpu(key_mgmt->count);
	if (left < ((int)sizeof(wpa_suite_auth_key_mgmt_t) +
		    (count) * (int)sizeof(wpa_suite)))
		return MFALSE;
	for (i = 0; i < count; i++) {
		switch (key_mgmt->list[i].type) {
		case RSN_AKM_8021X:
			sys_config->key_mgmt = KEY_MGMT_EAP;
			break;
		case RSN_AKM_PSK:
			sys_config->key_mgmt = KEY_MGMT_PSK;
			break;
		}
	}
	return MTRUE;
}

/**
 * @brief Find RSN/WPA IES
 *
 * @param ie              Pointer IE buffer
 * @param sys_config      Pointer to mlan_uap_bss_param structure
 *
 * @return                MTRUE/MFALSE
 */
static t_u8 woal_find_wpa_ies(const t_u8 *ie, int len,
			      mlan_uap_bss_param *sys_config)
{
	int bytes_left = len;
	const t_u8 *pcurrent_ptr = ie;
	t_u16 total_ie_len;
	t_u8 element_len;
	t_u8 wpa2 = 0;
	t_u8 wpa = 0;
	t_u8 ret = MFALSE;
	IEEEtypes_ElementId_e element_id;
	IEEEtypes_VendorSpecific_t *pvendor_ie;
	const t_u8 wpa_oui[4] = {0x00, 0x50, 0xf2, 0x01};

	while (bytes_left >= 2) {
		element_id = (IEEEtypes_ElementId_e)(*((t_u8 *)pcurrent_ptr));
		element_len = *((t_u8 *)pcurrent_ptr + 1);
		total_ie_len = element_len + sizeof(IEEEtypes_Header_t);
		if (bytes_left < total_ie_len) {
			PRINTM(MERROR,
			       "InterpretIE: Error in processing IE, bytes left < IE length\n");
			bytes_left = 0;
			continue;
		}
		switch (element_id) {
		case RSN_IE:
			wpa2 = woal_check_rsn_ie(
				(IEEEtypes_Rsn_t *)pcurrent_ptr, sys_config);
			break;
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorSpecific_t *)pcurrent_ptr;
			if (!memcmp(pvendor_ie->vend_hdr.oui, wpa_oui,
				    sizeof(pvendor_ie->vend_hdr.oui)) &&
			    (pvendor_ie->vend_hdr.oui_type == wpa_oui[3])) {
				wpa = woal_check_wpa_ie(
					(IEEEtypes_Wpa_t *)pcurrent_ptr,
					sys_config);
			}
			break;
		default:
			break;
		}
		pcurrent_ptr += element_len + 2;
		/* Need to account for IE ID and IE Len */
		bytes_left -= (element_len + 2);
	}
	if (wpa && wpa2) {
		sys_config->protocol = PROTOCOL_WPA | PROTOCOL_WPA2;
		ret = MTRUE;
	} else if (wpa2) {
		sys_config->protocol = PROTOCOL_WPA2;
		ret = MTRUE;
	} else if (wpa) {
		sys_config->protocol = PROTOCOL_WPA;
		ret = MTRUE;
	}
	return ret;
}

/**
 * @brief Find and set WMM IES
 *
 * @param priv            Pointer to moal_private
 * @param ie              Pointer IE buffer
 * @param sys_config      Pointer to mlan_uap_bss_param structure
 *
 * @return                N/A
 */
static t_void woal_set_wmm_ies(moal_private *priv, const t_u8 *ie, int len,
			       mlan_uap_bss_param *sys_config)
{
	int bytes_left = len;
	const t_u8 *pcurrent_ptr = ie;
	t_u16 total_ie_len;
	t_u8 element_len;
	IEEEtypes_VendorSpecific_t *pvendor_ie;
	IEEEtypes_ElementId_e element_id;
	const t_u8 wmm_oui[4] = {0x00, 0x50, 0xf2, 0x02};

	while (bytes_left >= 2) {
		element_id = (IEEEtypes_ElementId_e)(*((t_u8 *)pcurrent_ptr));
		element_len = *((t_u8 *)pcurrent_ptr + 1);
		total_ie_len = element_len + sizeof(IEEEtypes_Header_t);
		if (bytes_left < total_ie_len) {
			PRINTM(MERROR,
			       "InterpretIE: Error in processing IE, bytes left < IE length\n");
			bytes_left = 0;
			continue;
		}
		switch (element_id) {
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorSpecific_t *)pcurrent_ptr;
			if (!memcmp(pvendor_ie->vend_hdr.oui, wmm_oui,
				    sizeof(pvendor_ie->vend_hdr.oui)) &&
			    pvendor_ie->vend_hdr.oui_type == wmm_oui[3]) {
				if (total_ie_len ==
				    sizeof(IEEEtypes_WmmParameter_t)) {
					/*
					 * Only accept and copy the WMM IE if
					 * it matches the size expected for the
					 * WMM Parameter IE.
					 */
					moal_memcpy_ext(
						priv->phandle,
						&sys_config->wmm_para,
						pcurrent_ptr +
							sizeof(IEEEtypes_Header_t),
						element_len,
						sizeof(sys_config->wmm_para));
					/** set uap_host_based_config to true */
					sys_config->uap_host_based_config =
						MTRUE;
				}
			}

			break;
		default:
			break;
		}
		pcurrent_ptr += element_len + 2;
		/* Need to account for IE ID and IE Len */
		bytes_left -= (element_len + 2);
	}
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 * @brief initialize AP or GO bss config
 * @param priv            A pointer to moal private structure
 * @param band		  BAND_2GHZ/BAND_5GHZ
 * @param params          A pointer to cfg80211_ap_settings structure
 * @return                0 -- success, otherwise fail
 */
static t_u8 woal_check_11ac_capability(moal_private *priv, t_u8 band,
				       struct cfg80211_ap_settings *params)
#else
/**
 * @brief initialize AP or GO bss config
 * @param band		  BAND_2GHZ/BAND_5GHZ
 * @param priv            A pointer to moal private structure
 * @return                0 -- success, otherwise fail
 */
static t_u8 woal_check_11ac_capability(moal_private *priv, t_u8 band)
#endif
{
	mlan_fw_info fw_info;
	t_u8 enable_11ac = MFALSE;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	const u8 *vht_ie = NULL;
#endif
	ENTER();
	memset(&fw_info, 0, sizeof(mlan_fw_info));
	woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);
	if ((band == BAND_5GHZ) && !(fw_info.fw_bands & BAND_AAC)) {
		PRINTM(MCMND, "FW don't support 5G AC");
		LEAVE();
		return enable_11ac;
	}
	if ((band == BAND_2GHZ) && !(fw_info.fw_bands & BAND_GAC)) {
		PRINTM(MCMND, "FW don't support 2G AC");
		LEAVE();
		return enable_11ac;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	vht_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, params->beacon.tail,
				  params->beacon.tail_len);
	if (vht_ie)
		enable_11ac = MTRUE;
	else
		enable_11ac = MFALSE;
#else
	enable_11ac = MTRUE;
#endif
	LEAVE();
	return enable_11ac;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 * @brief initialize AP or GO bss config
 * @param priv            A pointer to moal private structure
 * @param band            BAND_5G/BAND_2GHZ
 * @param params          A pointer to cfg80211_ap_settings structure
 * @return                0 -- success, otherwise fail
 */
static t_u8 woal_check_11ax_capability(moal_private *priv, t_u8 band,
				       struct cfg80211_ap_settings *params)
{
	mlan_fw_info fw_info;
	t_u8 enable_11ax = MFALSE;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	mlan_ds_11ax_he_cfg he_cfg;
	t_u8 he_txrx_mcs_support[4] = {0xff, 0xff, 0xff, 0xff};
#endif
	ENTER();
	memset(&fw_info, 0, sizeof(mlan_fw_info));
	woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);
	if ((band == BAND_5GHZ) && !(fw_info.fw_bands & BAND_AAX)) {
		PRINTM(MCMND, "FW don't support 5G AX\n");
		LEAVE();
		return enable_11ax;
	}
	if ((band == BAND_2GHZ) && !(fw_info.fw_bands & BAND_GAX)) {
		PRINTM(MCMND, "FW don't support 2G AX");
		LEAVE();
		return enable_11ax;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	if (params->he_cap)
		enable_11ax = MTRUE;
	else
		enable_11ax = MFALSE;
#else
	if (priv->phandle->params.auto_11ax == 0) {
		PRINTM(MCMND, "auto_11ax is disabled\n");
		LEAVE();
		return enable_11ax;
	}
	memset(&he_cfg, 0, sizeof(he_cfg));
	if (band == BAND_5GHZ)
		he_cfg.band = MBIT(1);
	else if (band == BAND_2GHZ)
		he_cfg.band = MBIT(0);
	if (0 == woal_11ax_cfg(priv, MLAN_ACT_GET, &he_cfg, MOAL_IOCTL_WAIT)) {
		if (he_cfg.he_cap.len &&
		    (he_cfg.he_cap.ext_id == HE_CAPABILITY)) {
			if (memcmp(he_cfg.he_cap.he_txrx_mcs_support,
				   he_txrx_mcs_support,
				   sizeof(he_txrx_mcs_support)))
				enable_11ax = MTRUE;
		}
	}
#endif
	PRINTM(MCMND, "enable_11ax=%d\n", enable_11ax);
	LEAVE();
	return enable_11ax;
}
#endif

#if KERNEL_VERSION(4, 20, 0) <= CFG80211_VERSION_CODE
/**
 * @brief check channel width with HE capabilities
 * @param priv            A pointer to moal private structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @return                0 -- channel width supported, otherwise not supported
 */
static t_u8 woal_check_chan_width_capa(moal_private *priv,
				       struct cfg80211_chan_def *chandef)
{
	mlan_fw_info fw_info;
	mlan_ds_11ax_he_capa *phe_cap = NULL;
	ENTER();
	memset(&fw_info, 0, sizeof(mlan_fw_info));
	woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);
	if (chandef->chan->band == NL80211_BAND_5GHZ) {
		phe_cap = (mlan_ds_11ax_he_capa *)fw_info.hw_he_cap;
		if (((chandef->width == NL80211_CHAN_WIDTH_160) &&
		     (!(phe_cap->he_phy_cap[0] & MBIT(3)))) ||
		    ((chandef->width == NL80211_CHAN_WIDTH_80P80) &&
		     (!(phe_cap->he_phy_cap[0] & MBIT(4))))) {
			PRINTM(MCMND, "FW don't support %s in %s band",
			       (chandef->width == NL80211_CHAN_WIDTH_160) ?
				       "160MHz" :
				       "80+80 MHz",
			       (chandef->chan->band == NL80211_BAND_5GHZ) ?
				       "5G" :
				       "6G");
			LEAVE();
			return MFALSE;
		}
	}
	LEAVE();
	return MTRUE;
}
#endif

/**
 * @brief get ht_cap from beacon ie
 *
 * @param ie              Pointer to IEs
 * @param len             Total length of ie
 *
 * @return                ht_cap
 */
static t_u16 woal_get_htcap_info(const t_u8 *ie, int len)
{
	t_u16 ht_cap_info = 0;
	IEEEtypes_HTCap_t *htcap_ie = NULL;
	htcap_ie =
		(IEEEtypes_HTCap_t *)woal_parse_ie_tlv(ie, len, HT_CAPABILITY);
	if (htcap_ie) {
		/* hostap has converted ht_cap_info to little endian, here
		 * conver to host endian */
		ht_cap_info = woal_le16_to_cpu(htcap_ie->ht_cap.ht_cap_info);
		PRINTM(MINFO, "Get ht_cap from beacon ies: 0x%x\n",
		       ht_cap_info);
	}
	return ht_cap_info;
}

/**
 * @brief get vht_cap from beacon ie
 *
 * @param ie              Pointer to IEs
 * @param len             Total length of ie
 *
 * @return                Pointer to vht_cap ie
 */
static IEEEtypes_VHTCap_t *woal_get_vhtcap_info(const t_u8 *ie, int len)
{
	IEEEtypes_VHTCap_t *vhtcap_ie = NULL;
	vhtcap_ie = (IEEEtypes_VHTCap_t *)woal_parse_ie_tlv(ie, len,
							    VHT_CAPABILITY);
	if (vhtcap_ie)
		PRINTM(MMSG, "Get vht_cap from beacon ies: 0x%x\n",
		       vhtcap_ie->vht_cap.vht_cap_info);
	return vhtcap_ie;
}

/**
 * @brief get vht_oper from beacon ie
 *
 * @param ie              Pointer to IEs
 * @param len             Total length of ie
 *
 * @return                Pointer to vht_opr ie
 */
static IEEEtypes_VHTOprat_t *woal_get_vht_oprat_ie(const t_u8 *ie, int len)
{
	IEEEtypes_VHTOprat_t *vht_oprat_ie = NULL;
	vht_oprat_ie = (IEEEtypes_VHTOprat_t *)woal_parse_ie_tlv(ie, len,
								 VHT_OPERATION);
	if (vht_oprat_ie)
		PRINTM(MMSG,
		       "Get vht_oprat_ie from beacon ies: chan_width=%d\n",
		       vht_oprat_ie->chan_width);
	return vht_oprat_ie;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/** Starting Frequency for 11A band */
#define START_FREQ_11A_BAND 5000 /* in MHz */
/**
 * @brief convert cfg80211_chan_def to Band_Config
 *
 * @param priv            A pointer to moal private structure
 * @param bandcfg         A pointer to (Band_Config_t structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 *
 * @return                N/A
 */
static void woal_convert_chan_to_bandconfig(moal_private *priv,
					    Band_Config_t *bandcfg,
					    struct cfg80211_chan_def *chandef)
{
	ENTER();
	memset(bandcfg, 0, sizeof(Band_Config_t));
	switch (chandef->chan->band) {
	case NL80211_BAND_2GHZ:
		bandcfg->chanBand = BAND_2GHZ;
		break;
	case NL80211_BAND_5GHZ:
		bandcfg->chanBand = BAND_5GHZ;
		break;
	default:
		break;
	}
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandcfg->chanWidth = CHAN_BW_20MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandcfg->chanWidth = CHAN_BW_40MHZ;
		if (chandef->center_freq1 > chandef->chan->center_freq)
			bandcfg->chan2Offset = SEC_CHAN_ABOVE;
		else
			bandcfg->chan2Offset = SEC_CHAN_BELOW;
		break;
	case NL80211_CHAN_WIDTH_80:
		bandcfg->chan2Offset = woal_get_second_channel_offset(
			priv, chandef->chan->hw_value);
		bandcfg->chanWidth = CHAN_BW_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
	default:
		break;
	}
	PRINTM(MCMND,
	       "cfg80211 AP: channel=%d, chanBand=0x%x chanWidth=0x%x chan2Offset=0x%x\n",
	       chandef->chan->hw_value, bandcfg->chanBand, bandcfg->chanWidth,
	       bandcfg->chan2Offset);
	LEAVE();
	return;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/**
 * @brief Enable radar detect for DFS channel
 *
 * @param priv            A pointer to moal private structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @return                N/A
 */
static void woal_enable_dfs_support(moal_private *priv,
				    struct cfg80211_chan_def *chandef)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_11h_chan_rep_req *pchan_rpt_req = NULL;
	mlan_ds_11h_cfg *p11h_cfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();
	if (!(chandef->chan->flags & IEEE80211_CHAN_RADAR)) {
		PRINTM(MIOCTL, "No radar channel\n");
		LEAVE();
		return;
	}
	PRINTM(MIOCTL, "start Radar detect, chan %d , Bw %d \n",
	       chandef->chan->hw_value, chandef->width);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11h_cfg));
	if (NULL == req) {
		PRINTM(MIOCTL, "No Memory to allocate ioctl buffer\n");
		LEAVE();
		return;
	}
	p11h_cfg = (mlan_ds_11h_cfg *)req->pbuf;
	pchan_rpt_req = &p11h_cfg->param.chan_rpt_req;
	pchan_rpt_req->startFreq = 5000;
	pchan_rpt_req->chanNum = (t_u8)chandef->chan->hw_value;
	woal_convert_chan_to_bandconfig(priv, &pchan_rpt_req->bandcfg, chandef);
	pchan_rpt_req->host_based = MTRUE;
	pchan_rpt_req->millisec_dwell_time = 0;

	p11h_cfg->sub_command = MLAN_OID_11H_CHANNEL_CHECK;
	req->req_id = MLAN_IOCTL_11H_CFG;
	req->action = MLAN_ACT_SET;
	/* Send Channel Check command and wait until the report is ready */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return;
}
#endif

/**
 * @brief Prase supported rates from beacon data, set bss cfg accordingly
 *
 * @param priv                  A pointer to moal_private
 * @param bss_cfg            A pointer to bss configuration structure
 * @param head_ie            A pointer to beacon head IE buffer
 * @param head_len           head IE buffer length
 * @param tail_ie            A pointer to beacon tail IE buffer
 * @param tail_len           tail IE buffer length *
 * @return                   N/A
 */
static void woal_set_uap_rates(moal_private *priv, mlan_uap_bss_param *bss_cfg,
			       const t_u8 *head_ie, int head_len,
			       const t_u8 *tail_ie, int tail_len)
{
	pIEEEtypes_Header_t rate_ie;
	pIEEEtypes_Header_t ext_rate_ie;
	int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *var_pos = head_ie + var_offset;
	int len = 0;
	int rate_len = 0;

	if (var_offset > head_len)
		return;
	len = head_len - var_offset;
	rate_ie = (void *)woal_parse_ie_tlv(var_pos, len, WLAN_EID_SUPP_RATES);
	if (rate_ie) {
		memset(bss_cfg->rates, 0, sizeof(bss_cfg->rates));
		moal_memcpy_ext(priv->phandle, bss_cfg->rates, rate_ie + 1,
				rate_ie->len, sizeof(bss_cfg->rates));
		rate_len = MIN(rate_ie->len, sizeof(bss_cfg->rates));
	}
	ext_rate_ie = (void *)woal_parse_ie_tlv(tail_ie, tail_len,
						WLAN_EID_EXT_SUPP_RATES);
	if (ext_rate_ie) {
		moal_memcpy_ext(priv->phandle, &bss_cfg->rates[rate_len],
				ext_rate_ie + 1, ext_rate_ie->len,
				sizeof(bss_cfg->rates) - rate_len);
		rate_len += MIN(ext_rate_ie->len,
				(sizeof(bss_cfg->rates) - rate_len));
	}
	DBG_HEXDUMP(MCMD_D, "rates", bss_cfg->rates, sizeof(bss_cfg->rates));
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
/**
 * @brief initialize AP or GO bss config
 *
 * @param priv            A pointer to moal private structure
 * @param params          A pointer to cfg80211_ap_settings structure
 * @return                0 -- success, otherwise fail
 */
static int woal_cfg80211_beacon_config(moal_private *priv,
				       struct cfg80211_ap_settings *params)
#else
/**
 * @brief initialize AP or GO bss config
 *
 * @param priv            A pointer to moal private structure
 * @param params          A pointer to beacon_parameters structure
 * @return                0 -- success, otherwise fail
 */
static int woal_cfg80211_beacon_config(moal_private *priv,
				       struct beacon_parameters *params)
#endif
{
	struct wiphy *wiphy = NULL;
	const t_u8 *ie = NULL;
	int ret = 0, ie_len;
	mlan_uap_bss_param *sys_config = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	int i = 0;
#else
	t_u8 wpa_ies;
	const t_u8 *ssid_ie = NULL;
	struct ieee80211_mgmt *head = NULL;
	t_u16 capab_info = 0;
#endif
	t_u8 rates_bg[13] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18,
			     0x24, 0x30, 0x48, 0x60, 0x6c, 0x00};
	t_u8 rates_a[9] = {0x8c, 0x12, 0x98, 0x24, 0xb0,
			   0x48, 0x60, 0x6c, 0x00};
#ifdef WIFI_DIRECT_SUPPORT
	t_u8 rates_wfd[9] = {0x8c, 0x12, 0x18, 0x24, 0x30,
			     0x48, 0x60, 0x6c, 0x00};
#endif
	t_u8 chan2Offset = SEC_CHAN_NONE;
	t_u8 enable_11n = MTRUE;
	t_u16 ht_cap = 0;
	t_u8 enable_11ac = MFALSE;
	t_u8 vht20_40 = MFALSE;
	IEEEtypes_VHTCap_t *vhtcap_ie = NULL;
	IEEEtypes_VHTOprat_t *vhtopr_ie = NULL;
	IEEEtypes_HECap_t *hecap_ie = NULL;
	t_u8 enable_11ax = MFALSE;
	t_u8 *wapi_ie = NULL;
	int wapi_ie_len = 0;
#ifdef WIFI_DIRECT_SUPPORT
	int GoAgeoutTime = priv->phandle->params.GoAgeoutTime;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	Band_Config_t bandcfg;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	mlan_ds_11h_chan_nop_info chan_nop_info;
#endif
	t_u8 wpa3_sae = 0;
	t_u8 *rsnx_ie = NULL;
	ENTER();

	if (!params) {
		ret = -EFAULT;
		goto done;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	ie = ((struct cfg80211_ap_settings *)params)->beacon.tail;
	ie_len = (int)((struct cfg80211_ap_settings *)params)->beacon.tail_len;
#else
	ie = ((struct beacon_parameters *)params)->tail;
	ie_len = (int)((struct beacon_parameters *)params)->tail_len;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	wapi_ie =
		(t_u8 *)woal_parse_ie_tlv(params->beacon.tail, ie_len, WAPI_IE);
#else
	wapi_ie = (t_u8 *)woal_parse_ie_tlv(params->tail, ie_len, WAPI_IE);
#endif
	if (wapi_ie) {
		wapi_ie_len = *(wapi_ie + 1) + 2;
		if (MLAN_STATUS_FAILURE ==
		    woal_set_get_gen_ie(priv, MLAN_ACT_SET, wapi_ie,
					&wapi_ie_len, MOAL_IOCTL_WAIT)) {
			PRINTM(MERROR, "Failed to set wapi ie\n");
			ret = -EFAULT;
			goto done;
		}
	}

	wiphy = priv->phandle->wiphy;
	if (priv->bss_type != MLAN_BSS_TYPE_UAP
#ifdef WIFI_DIRECT_SUPPORT
	    && priv->bss_type != MLAN_BSS_TYPE_WIFIDIRECT
#endif
	) {
		ret = -EFAULT;
		goto done;
	}
	sys_config = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_config) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		ret = -EFAULT;
		goto done;
	}

	/* Initialize the uap bss values which are uploaded from firmware */
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv, MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   sys_config)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

	if (priv->phandle->params.uap_max_sta)
		sys_config->max_sta_count = priv->phandle->params.uap_max_sta;

	/* Setting the default values */
	sys_config->channel = 6;
	sys_config->preamble_type = 0;
	sys_config->mgmt_ie_passthru_mask = priv->mgmt_subtype_mask;
	moal_memcpy_ext(priv->phandle, sys_config->mac_addr, priv->current_addr,
			ETH_ALEN, sizeof(sys_config->mac_addr));

#ifdef WIFI_DIRECT_SUPPORT
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT && GoAgeoutTime) {
		sys_config->sta_ageout_timer = GoAgeoutTime;
		sys_config->ps_sta_ageout_timer = GoAgeoutTime;
	}
#endif
	/* Set frag_threshold, rts_threshold, and retry limit */
	sys_config->frag_threshold = wiphy->frag_threshold;
	sys_config->rts_threshold = wiphy->rts_threshold;
	sys_config->retry_limit = wiphy->retry_long;
	if (sys_config->frag_threshold == (t_u16)MLAN_FRAG_RTS_DISABLED) {
		sys_config->frag_threshold = MLAN_FRAG_MAX_VALUE;
	}
	if (sys_config->rts_threshold == (t_u16)MLAN_FRAG_RTS_DISABLED) {
		sys_config->rts_threshold = MLAN_RTS_MAX_VALUE;
	}

	if (priv->bss_type == MLAN_BSS_TYPE_UAP) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
		if (params->beacon_interval)
			sys_config->beacon_period = params->beacon_interval;
#else
		if (params->interval)
			sys_config->beacon_period = params->interval;
#endif
		if (params->dtim_period)
			sys_config->dtim_period = params->dtim_period;
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	/** back up ap's channel */
	moal_memcpy_ext(priv->phandle, &priv->chan, &params->chandef,
			sizeof(struct cfg80211_chan_def), sizeof(priv->chan));
#endif

#if KERNEL_VERSION(4, 20, 0) <= CFG80211_VERSION_CODE
	if (!woal_check_chan_width_capa(priv, &params->chandef)) {
		ret = -EFAULT;
		goto done;
	}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	woal_convert_chan_to_bandconfig(priv, &bandcfg, &params->chandef);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	if (priv->phandle->usr_nop_period_sec) {
		PRINTM(MCMND, "Checking if AP's channel %d is under NOP\n",
		       priv->channel);
		memset(&chan_nop_info, 0, sizeof(chan_nop_info));
		chan_nop_info.curr_chan = priv->channel;
		chan_nop_info.chan_width = bandcfg.chanWidth;
		if (params->chandef.width >= NL80211_CHAN_WIDTH_20)
			chan_nop_info.new_chan.is_11n_enabled = MTRUE;
		chan_nop_info.new_chan.bandcfg = bandcfg;
		chan_nop_info.check_new_chan = MTRUE;
		woal_uap_get_channel_nop_info(priv, MOAL_IOCTL_WAIT,
					      &chan_nop_info);
		if (chan_nop_info.chan_under_nop) {
			PRINTM(MCMND,
			       "cfg80211: Channel %d is under NOP, New channel=%d\n",
			       priv->channel, chan_nop_info.new_chan.channel);
			priv->chan_under_nop = chan_nop_info.chan_under_nop;
			priv->channel = chan_nop_info.new_chan.channel;
			priv->bandwidth =
				chan_nop_info.new_chan.bandcfg.chanWidth;
			woal_chandef_create(priv, &priv->chan,
					    &chan_nop_info.new_chan);
		}
	}
#endif

	if (priv->channel) {
		woal_check_mc_connection(priv, MOAL_IOCTL_WAIT, priv->channel);
		memset(sys_config->rates, 0, sizeof(sys_config->rates));
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		switch (priv->chan.width) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
#endif
		case NL80211_CHAN_WIDTH_20_NOHT:
			enable_11n = MFALSE;
			break;
		case NL80211_CHAN_WIDTH_20:
			break;
		case NL80211_CHAN_WIDTH_40:
			if (priv->chan.center_freq1 <
			    priv->chan.chan->center_freq)
				chan2Offset = SEC_CHAN_BELOW;
			else
				chan2Offset = SEC_CHAN_ABOVE;
			break;
		case NL80211_CHAN_WIDTH_80:
		case NL80211_CHAN_WIDTH_80P80:
		case NL80211_CHAN_WIDTH_160:
			chan2Offset = woal_get_second_channel_offset(
				priv, priv->channel);
			break;
		default:
			PRINTM(MWARN, "Unknown channel width: %d\n",
			       priv->chan.width);
			break;
		}
#else
		switch (params->channel_type) {
		case NL80211_CHAN_NO_HT:
			enable_11n = MFALSE;
			break;
		case NL80211_CHAN_HT20:
			break;
		case NL80211_CHAN_HT40PLUS:
			chan2Offset = SEC_CHAN_ABOVE;
			break;
		case NL80211_CHAN_HT40MINUS:
			chan2Offset = SEC_CHAN_BELOW;
			break;
		default:
			PRINTM(MWARN, "Unknown channel type: %d\n",
			       params->channel_type);
			break;
		}
#endif
#endif /* CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) */

		sys_config->channel = priv->channel;
		if (priv->channel <= MAX_BG_CHANNEL) {
			sys_config->bandcfg.chanBand = BAND_2GHZ;
#ifdef WIFI_DIRECT_SUPPORT
			if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
				moal_memcpy_ext(priv->phandle,
						sys_config->rates, rates_wfd,
						sizeof(rates_wfd),
						sizeof(sys_config->rates));
			else
#endif
				moal_memcpy_ext(priv->phandle,
						sys_config->rates, rates_bg,
						sizeof(rates_bg),
						sizeof(sys_config->rates));
		} else {
			sys_config->bandcfg.chanBand = BAND_5GHZ;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
			chan2Offset = woal_get_second_channel_offset(
				priv, priv->channel);
#endif

#ifdef WIFI_DIRECT_SUPPORT
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
			/* Force enable 40MHZ on WFD interface */
			if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
				chan2Offset = woal_get_second_channel_offset(
					priv, priv->channel);
#endif
#endif
#ifdef WIFI_DIRECT_SUPPORT
			if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
				moal_memcpy_ext(priv->phandle,
						sys_config->rates, rates_wfd,
						sizeof(rates_wfd),
						sizeof(sys_config->rates));
			else
#endif
				moal_memcpy_ext(priv->phandle,
						sys_config->rates, rates_a,
						sizeof(rates_a),
						sizeof(sys_config->rates));
		}

		/* Replaced with rate from userspace, if exist */
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
		woal_set_uap_rates(priv, sys_config, params->beacon.head,
				   params->beacon.head_len, params->beacon.tail,
				   params->beacon.tail_len);
#else
		woal_set_uap_rates(priv, sys_config, params->head,
				   params->head_len, params->tail,
				   params->tail_len);
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		moal_memcpy_ext(priv->phandle, &sys_config->bandcfg, &bandcfg,
				sizeof(bandcfg), sizeof(bandcfg));
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		enable_11ac = woal_check_11ac_capability(
			priv, sys_config->bandcfg.chanBand, params);
		if (enable_11ac &&
		    ((priv->chan.width == NL80211_CHAN_WIDTH_20) ||
		     (priv->chan.width == NL80211_CHAN_WIDTH_40)))
			vht20_40 = MTRUE;
#else
		enable_11ac = woal_check_11ac_capability(
			priv, sys_config->bandcfg.chanBand);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		enable_11ax = woal_check_11ax_capability(
			priv, sys_config->bandcfg.chanBand, params);
#endif

		/* Disable GreenField by default */
		sys_config->ht_cap_info = 0x10c;
		if (enable_11n)
			sys_config->ht_cap_info |= 0x20;
		if (chan2Offset) {
			sys_config->bandcfg.chan2Offset = chan2Offset;
			sys_config->ht_cap_info |= 0x1042;
			sys_config->ampdu_param = 3;
		} else {
			sys_config->bandcfg.chan2Offset = 0;
		}
		ht_cap = woal_get_htcap_info(ie, ie_len);
		if (ht_cap) {
			if (sys_config->bandcfg.chanBand == BAND_2GHZ)
				sys_config->ht_cap_info =
					(ht_cap &
					 (wiphy->bands[IEEE80211_BAND_2GHZ]
						  ->ht_cap.cap &
					  0x13ff)) |
					0x0c;
			else
				sys_config->ht_cap_info =
					(ht_cap &
					 (wiphy->bands[IEEE80211_BAND_5GHZ]
						  ->ht_cap.cap &
					  0x13ff)) |
					0x0c;
		}
		PRINTM(MCMND,
		       "11n=%d, ht_cap=0x%x, channel=%d, bandcfg:chanBand=0x%x chanWidth=0x%x chan2Offset=0x%x scanMode=0x%x\n",
		       enable_11n, sys_config->ht_cap_info, priv->channel,
		       sys_config->bandcfg.chanBand,
		       sys_config->bandcfg.chanWidth,
		       sys_config->bandcfg.chan2Offset,
		       sys_config->bandcfg.scanMode);
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	if (!params->ssid || !params->ssid_len) {
		ret = -EINVAL;
		goto done;
	}
	moal_memcpy_ext(priv->phandle, sys_config->ssid.ssid, params->ssid,
			MIN(MLAN_MAX_SSID_LENGTH, params->ssid_len),
			sizeof(sys_config->ssid.ssid));
	sys_config->ssid.ssid_len = MIN(MLAN_MAX_SSID_LENGTH, params->ssid_len);
	/**
	 * hidden_ssid=0: broadcast SSID in beacons.
	 * hidden_ssid=1: send empty SSID (length=0) in beacon.
	 * hidden_ssid=2: clear SSID (ACSII 0), but keep the original length
	 */
	if (!params->hidden_ssid)
		sys_config->bcast_ssid_ctl = 1;
	else if (params->hidden_ssid == 1)
		sys_config->bcast_ssid_ctl = 0;
	else if (params->hidden_ssid == 2)
		sys_config->bcast_ssid_ctl = 2;
	switch (params->auth_type) {
	case NL80211_AUTHTYPE_SHARED_KEY:
		sys_config->auth_mode = MLAN_AUTH_MODE_SHARED;
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		sys_config->auth_mode = MLAN_AUTH_MODE_AUTO;
		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	default:
		sys_config->auth_mode = MLAN_AUTH_MODE_OPEN;
		break;
	}

	sys_config->protocol = PROTOCOL_NO_SECURITY;
	if ((params->crypto.wpa_versions & NL80211_WPA_VERSION_1) &&
	    (params->crypto.wpa_versions & NL80211_WPA_VERSION_2))
		sys_config->protocol = PROTOCOL_WPA | PROTOCOL_WPA2;
	else if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		sys_config->protocol = PROTOCOL_WPA2;
	else if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		sys_config->protocol = PROTOCOL_WPA;
	if (params->crypto.n_akm_suites ||
	    (params->privacy && params->crypto.wpa_versions))
		woal_find_wpa_ies(ie, ie_len, sys_config);
	for (i = 0; i < params->crypto.n_akm_suites; i++) {
		switch (params->crypto.akm_suites[i]) {
		case WLAN_AKM_SUITE_8021X:
			sys_config->key_mgmt |= KEY_MGMT_EAP;
			break;
		case WLAN_AKM_SUITE_PSK:
			sys_config->key_mgmt |= KEY_MGMT_PSK;
			break;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
		case WLAN_AKM_SUITE_SAE:
		case WLAN_AKM_SUITE_FT_OVER_SAE:
			wpa3_sae = MTRUE;
			break;
#endif
		}
	}
	if (wpa3_sae) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
		rsnx_ie = (t_u8 *)woal_parse_ie_tlv(params->beacon.tail, ie_len,
						    RSNX_IE);
#else
		rsnx_ie = (t_u8 *)woal_parse_ie_tlv(params->tail, ie_len,
						    RSNX_IE);
#endif
		if (rsnx_ie)
			sys_config->pwe_derivation = SAE_PWE_BOTH;
		else
			sys_config->pwe_derivation = SAE_PWE_HUNT_AND_PECK;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		switch (params->crypto.sae_pwe) {
		case NL80211_SAE_PWE_HUNT_AND_PECK:
			sys_config->pwe_derivation = SAE_PWE_HUNT_AND_PECK;
			break;
		case NL80211_SAE_PWE_HASH_TO_ELEMENT:
			sys_config->pwe_derivation = SAE_PWE_HASH_TO_ELEMENT;
			break;
		case NL80211_SAE_PWE_BOTH:
			sys_config->pwe_derivation = SAE_PWE_BOTH;
			break;
		default:
			break;
		}
#endif
	}

	sys_config->wpa_cfg.pairwise_cipher_wpa = 0;
	sys_config->wpa_cfg.pairwise_cipher_wpa2 = 0;
	for (i = 0; i < params->crypto.n_ciphers_pairwise; i++) {
		switch (params->crypto.ciphers_pairwise[i]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
				sys_config->wpa_cfg.pairwise_cipher_wpa |=
					CIPHER_TKIP;
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
				sys_config->wpa_cfg.pairwise_cipher_wpa2 |=
					CIPHER_TKIP;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
				sys_config->wpa_cfg.pairwise_cipher_wpa |=
					CIPHER_AES_CCMP;
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
				sys_config->wpa_cfg.pairwise_cipher_wpa2 |=
					CIPHER_AES_CCMP;
			break;
		case WLAN_CIPHER_SUITE_SMS4:
			sys_config->protocol = PROTOCOL_WAPI;
			break;
		}
	}
	switch (params->crypto.cipher_group) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if ((priv->cipher == WLAN_CIPHER_SUITE_WEP40) ||
		    (priv->cipher == WLAN_CIPHER_SUITE_WEP104)) {
			sys_config->protocol = PROTOCOL_STATIC_WEP;
			sys_config->key_mgmt = KEY_MGMT_NONE;
			sys_config->wpa_cfg.length = 0;
			moal_memcpy_ext(priv->phandle,
					&sys_config->wep_cfg.key0,
					&priv->uap_wep_key[0], sizeof(wep_key),
					sizeof(sys_config->wep_cfg.key0));
			moal_memcpy_ext(priv->phandle,
					&sys_config->wep_cfg.key1,
					&priv->uap_wep_key[1], sizeof(wep_key),
					sizeof(sys_config->wep_cfg.key1));
			moal_memcpy_ext(priv->phandle,
					&sys_config->wep_cfg.key2,
					&priv->uap_wep_key[2], sizeof(wep_key),
					sizeof(sys_config->wep_cfg.key2));
			moal_memcpy_ext(priv->phandle,
					&sys_config->wep_cfg.key3,
					&priv->uap_wep_key[3], sizeof(wep_key),
					sizeof(sys_config->wep_cfg.key3));
		}
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		sys_config->wpa_cfg.group_cipher = CIPHER_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sys_config->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
		break;
	case WLAN_CIPHER_SUITE_SMS4:
		sys_config->protocol = PROTOCOL_WAPI;
		break;
	}
#else
/* Since in Android ICS 4.0.1's wpa_supplicant, there is no way to set ssid
 * when GO (AP) starts up, so get it from beacon head parameter
 * TODO: right now use hard code
 * 24 -- ieee80211 header lenth, 12 -- fixed element length for beacon
 */
#define BEACON_IE_OFFSET 36
	/* Find SSID in head
	 * SSID IE id: 0, right now use hard code
	 */
	ssid_ie = woal_parse_ie_tlv(params->head + BEACON_IE_OFFSET,
				    params->head_len - BEACON_IE_OFFSET, 0);

	if (!ssid_ie) {
		PRINTM(MERROR, "No ssid IE found.\n");
		ret = -EFAULT;
		goto done;
	}
	if (*(ssid_ie + 1) > 32) {
		PRINTM(MERROR, "ssid len error: %d\n", *(ssid_ie + 1));
		ret = -EFAULT;
		goto done;
	}
	moal_memcpy_ext(priv->phandle, sys_config->ssid.ssid, ssid_ie + 2,
			*(ssid_ie + 1), sizeof(sys_config->ssid.ssid));
	sys_config->ssid.ssid_len = *(ssid_ie + 1);
	head = (struct ieee80211_mgmt *)params->head;

	capab_info = le16_to_cpu(head->u.beacon.capab_info);
	PRINTM(MIOCTL, "capab_info=0x%x\n", head->u.beacon.capab_info);
	sys_config->auth_mode = MLAN_AUTH_MODE_OPEN;
	/** For ICS, we don't support OPEN mode */
	if ((priv->cipher == WLAN_CIPHER_SUITE_WEP40) ||
	    (priv->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		sys_config->protocol = PROTOCOL_STATIC_WEP;
		sys_config->key_mgmt = KEY_MGMT_NONE;
		sys_config->wpa_cfg.length = 0;
		moal_memcpy_ext(priv->phandle, &sys_config->wep_cfg.key0,
				&priv->uap_wep_key[0], sizeof(wep_key),
				sizeof(sys_config->wep_cfg.key0));
		moal_memcpy_ext(priv->phandle, &sys_config->wep_cfg.key1,
				&priv->uap_wep_key[1], sizeof(wep_key),
				sizeof(sys_config->wep_cfg.key1));
		moal_memcpy_ext(priv->phandle, &sys_config->wep_cfg.key2,
				&priv->uap_wep_key[2], sizeof(wep_key),
				sizeof(sys_config->wep_cfg.key2));
		moal_memcpy_ext(priv->phandle, &sys_config->wep_cfg.key3,
				&priv->uap_wep_key[3], sizeof(wep_key),
				sizeof(sys_config->wep_cfg.key3));
	} else {
		/** Get cipher and key_mgmt from RSN/WPA IE */
		if (capab_info & WLAN_CAPABILITY_PRIVACY) {
			wpa_ies = woal_find_wpa_ies(
				params->tail, params->tail_len, sys_config);
			if (wpa_ies == MFALSE) {
				/* hard code setting to wpa2-psk */
				sys_config->protocol = PROTOCOL_WPA2;
				sys_config->key_mgmt = KEY_MGMT_PSK;
				sys_config->wpa_cfg.pairwise_cipher_wpa2 =
					CIPHER_AES_CCMP;
				sys_config->wpa_cfg.group_cipher =
					CIPHER_AES_CCMP;
			}
		}
	}
#endif

	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		/*find and set wmm ie*/
		woal_set_wmm_ies(priv, ie, ie_len, sys_config);
	}

	/** If the security mode is configured as WEP or
	 * WPA/WPA2 with TKIP only, disable 11n*/
	if (sys_config->protocol == PROTOCOL_STATIC_WEP)
		enable_11n = MFALSE;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	else if (sys_config->protocol & (PROTOCOL_WPA | PROTOCOL_WPA2)) {
		if ((params->crypto.wpa_versions & NL80211_WPA_VERSION_1) &&
		    sys_config->wpa_cfg.pairwise_cipher_wpa == CIPHER_TKIP)
			enable_11n = MFALSE;

		if ((params->crypto.wpa_versions & NL80211_WPA_VERSION_2) &&
		    sys_config->wpa_cfg.pairwise_cipher_wpa2 == CIPHER_TKIP)
			enable_11n = MFALSE;
	}
#endif

	if (!enable_11n) {
		if (woal_set_uap_ht_tx_cfg(priv, sys_config->bandcfg, ht_cap,
					   MFALSE)) {
			PRINTM(MMSG, "woal_set_uap_ht_tx_cfg fail\n");
			ret = -EFAULT;
			goto done;
		}
		woal_uap_set_11n_status(priv, sys_config, MLAN_ACT_DISABLE);
	} else {
		if (woal_set_uap_ht_tx_cfg(priv, sys_config->bandcfg, ht_cap,
					   MTRUE)) {
			PRINTM(MMSG, "woal_set_uap_ht_tx_cfg fail\n");
			ret = -EFAULT;
			goto done;
		}
		woal_uap_set_11n_status(priv, sys_config, MLAN_ACT_ENABLE);
		woal_set_get_tx_bf_cap(priv, MLAN_ACT_GET,
				       &sys_config->tx_bf_cap);
	}
	if (enable_11ac && enable_11n) {
		vhtcap_ie = woal_get_vhtcap_info(ie, ie_len);
		vhtopr_ie = woal_get_vht_oprat_ie(ie, ie_len);
		// Enable VHT80
		if (vhtopr_ie && vhtopr_ie->chan_width)
			vht20_40 = 0;
		woal_uap_set_11ac_status(priv, MLAN_ACT_ENABLE, vht20_40,
					 vhtcap_ie);
	} else {
		woal_uap_set_11ac_status(priv, MLAN_ACT_DISABLE, vht20_40,
					 NULL);
	}
	if (enable_11ax) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
		hecap_ie = (IEEEtypes_HECap_t *)woal_parse_ext_ie_tlv(
			ie, ie_len, HE_CAPABILITY);
#if CFG80211_VERSION_CODE > KERNEL_VERSION(5, 3, 0)
		if (params->twt_responder == MFALSE) {
			hecap_ie->he_mac_cap[0] &= ~HE_MAC_CAP_TWT_RESP_SUPPORT;
		}
#endif
#endif
		woal_uap_set_11ax_status(priv, MLAN_ACT_ENABLE,
					 sys_config->bandcfg.chanBand,
					 hecap_ie);
	} else
		woal_uap_set_11ax_status(priv, MLAN_ACT_DISABLE,
					 sys_config->bandcfg.chanBand, NULL);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	if (params->inactivity_timeout) {
		sys_config->sta_ageout_timer = params->inactivity_timeout * 10;
		sys_config->ps_sta_ageout_timer =
			params->inactivity_timeout * 10;
	}
	PRINTM(MIOCTL, "inactivity_timeout=%d\n", params->inactivity_timeout);
	PRINTM(MIOCTL, "sta_ageout_timer=%d ps_sta_ageout_timer=%d\n",
	       sys_config->sta_ageout_timer, sys_config->ps_sta_ageout_timer);
#endif

	if (priv->multi_ap_flag) {
		sys_config->multi_ap_flag = priv->multi_ap_flag;
		PRINTM(MINFO, "%s: multi_ap_flag is 0x%x\n", __func__,
		       sys_config->multi_ap_flag);
	}
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv, MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_config)) {
		ret = -EFAULT;
		goto done;
	}

	/** Set wacp_mode for uAP/P2P-GO */
	if (priv->phandle->params.wacp_mode) {
		PRINTM(MIOCTL, "wacp_mode: %d\n",
		       priv->phandle->params.wacp_mode);
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_wacp_mode(priv, MOAL_IOCTL_WAIT)) {
			PRINTM(MERROR, "Set wacp_mode failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	woal_enable_dfs_support(priv, &priv->chan);
#endif
done:
	kfree(sys_config);
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/**
 * @brief Request the driver to add a monitor interface
 *
 * @param wiphy             A pointer to wiphy structure
 * @param name              Virtual interface name
 * @param name_assign_type  Interface name assignment type
 * @param flags             Flags for the virtual interface
 * @param params            A pointer to vif_params structure
 * @param new_dev           Netdevice to be passed out
 *
 * @return                  0 -- success, otherwise fail
 */
static int woal_cfg80211_add_mon_if(struct wiphy *wiphy, const char *name,
				    unsigned char name_assign_type, u32 *flags,
				    struct vif_params *params,
				    struct net_device **new_dev)
#else
/**
 * @brief Request the driver to add a monitor interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 * @param new_dev         Netdevice to be passed out
 *
 * @return                0 -- success, otherwise fail
 */
static int woal_cfg80211_add_mon_if(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
				    const
#endif
				    char *name,
				    u32 *flags, struct vif_params *params,
				    struct net_device **new_dev)
#endif
{
	int ret = 0;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv =
		(moal_private *)woal_get_priv(handle, MLAN_BSS_ROLE_STA);
	monitor_iface *mon_if = NULL;
	struct net_device *ndev = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	chan_band_info chan_info;
#endif
	unsigned char name_assign_type_tmp = 0;

	ENTER();

	ASSERT_RTNL();

	if (handle->mon_if) {
		PRINTM(MERROR, "%s: monitor interface exist: %s basedev %s\n",
		       __func__, handle->mon_if->mon_ndev->name,
		       handle->mon_if->base_ndev->name);
		ret = -EFAULT;
		goto fail;
	}
	if (!priv) {
		PRINTM(MERROR, "add_mon_if: priv is NULL\n");
		ret = -EFAULT;
		goto fail;
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	name_assign_type_tmp = name_assign_type;
#endif
	mon_if = woal_prepare_mon_if(priv, name, name_assign_type_tmp);
	if (!mon_if) {
		PRINTM(MFATAL, "Prepare mon_if fail.\n");
		goto fail;
	}
	ndev = mon_if->mon_ndev;
	dev_net_set(ndev, wiphy_net(wiphy));

	moal_memcpy_ext(priv->phandle, ndev->perm_addr, wiphy->perm_addr,
			ETH_ALEN, sizeof(ndev->perm_addr));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	eth_hw_addr_set(ndev, ndev->perm_addr);
#else
	moal_memcpy_ext(priv->phandle, ndev->dev_addr, ndev->perm_addr,
			ETH_ALEN, MAX_ADDR_LEN);
#endif
	SET_NETDEV_DEV(ndev, wiphy_dev(wiphy));
	ndev->ieee80211_ptr = &mon_if->wdev;
	mon_if->wdev.iftype = NL80211_IFTYPE_MONITOR;
	mon_if->wdev.wiphy = wiphy;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	memset(&chan_info, 0x00, sizeof(chan_info));
	mon_if->band_chan_cfg.channel = 0;
	if (!woal_is_any_interface_active(handle)) {
		/* Set default band channel config */
		mon_if->band_chan_cfg.band = BAND_B | BAND_G;
		mon_if->band_chan_cfg.band |= BAND_GN;
		mon_if->band_chan_cfg.channel = 1;
		mon_if->band_chan_cfg.chan_bandwidth = CHANNEL_BW_20MHZ;
		memset(&chan_info, 0x00, sizeof(chan_info));
		chan_info.channel = 1;
		chan_info.is_11n_enabled = MTRUE;
	}
	mon_if->flag = handle->params.mon_filter;
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_net_monitor(priv, MOAL_IOCTL_WAIT, MTRUE, mon_if->flag,
				 &mon_if->band_chan_cfg)) {
		PRINTM(MERROR, "%s: woal_set_net_monitor fail\n", __func__);
		ret = -EFAULT;
		goto fail;
	}
	if (woal_is_any_interface_active(handle)) {
		/* set current band channel config */
		chan_info.bandcfg.chanBand = mon_if->band_chan_cfg.band;
		if (mon_if->band_chan_cfg.band &
		    (BAND_B | BAND_G | BAND_GN | BAND_GAC))
			chan_info.bandcfg.chanBand = BAND_2GHZ;
		else
			/* TODO: Add support for BAND_4GHZ */
			chan_info.bandcfg.chanBand = BAND_5GHZ;
		chan_info.bandcfg.chanWidth =
			mon_if->band_chan_cfg.chan_bandwidth;
		chan_info.channel = mon_if->band_chan_cfg.channel;
		chan_info.is_11n_enabled = MTRUE;
	}
	if (MLAN_STATUS_FAILURE ==
	    woal_chandef_create(priv, &mon_if->chandef, &chan_info)) {
		/* stop monitor mode on error */
		woal_set_net_monitor(priv, MOAL_IOCTL_WAIT, MFALSE, 0, NULL);
		ret = -EFAULT;
		goto fail;
	}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	ret = cfg80211_register_netdevice(ndev);
#else
	ret = register_netdevice(ndev);
#endif

	if (ret) {
		PRINTM(MFATAL, "register net_device failed, ret=%d\n", ret);
		free_netdev(ndev);
		goto fail;
	}

	handle->mon_if = mon_if;

	if (new_dev)
		*new_dev = ndev;

fail:
	LEAVE();
	return ret;
}

/**
 *  @brief This function setup the multi-ap virtual interface
 *
 *  @param dev    A pointer to structure net_device
 *
 *  @return       N/A
 */
static void woal_vlan_virt_if_setup(struct net_device *dev)
{
	ENTER();
	ether_setup(dev);
	dev->netdev_ops = &woal_uap_netdev_ops;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 11, 9)
	dev->needs_free_netdev = true;
#else
	dev->destructor = free_netdev;
#endif
	LEAVE();
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/**
 * @brief Request the driver to add a multi-ap virtual interface
 *
 * @param wiphy             A pointer to wiphy structure
 * @param name              Virtual interface name
 * @param name_assign_type  Interface name assignment type
 * @param flags             Flags for the virtual interface
 * @param params            A pointer to vif_params structure
 * @param new_dev		    new net_device to return
 *
 * @return                  0 -- success, otherwise fail
 */
static int woal_cfg80211_add_vlan_vir_if(struct wiphy *wiphy, const char *name,
					 unsigned char name_assign_type,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
					 u32 *flags,
#endif
					 struct vif_params *params,
					 struct net_device **new_dev)
#else
/**
 * @brief Request the driver to add a multi-ap virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 * @param new_dev		  new net_device to return
 *
 * @return                0 -- success, otherwise fail
 */
static int woal_cfg80211_add_vlan_vir_if(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
					 const
#endif
					 char *name,
					 u32 *flags, struct vif_params *params,
					 struct net_device **new_dev)
#endif
{
	int ret = 0;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv =
		(moal_private *)woal_get_priv(handle, MLAN_BSS_ROLE_UAP);
	moal_private *new_priv = NULL;
	struct net_device *ndev = NULL;

	ENTER();
	if (!priv) {
		PRINTM(MFATAL, "Error:woal_get_priv returned NULL\n");
		ret = -EFAULT;
		goto fail;
	}
	ASSERT_RTNL();
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#ifndef MAX_WMM_QUEUE
#define MAX_WMM_QUEUE 4
#endif
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	ndev = alloc_netdev_mq(sizeof(moal_private), name, name_assign_type,
			       woal_vlan_virt_if_setup, 1);
#else
	ndev = alloc_netdev_mq(sizeof(moal_private), name, NET_NAME_UNKNOWN,
			       woal_vlan_virt_if_setup, 1);
#endif
#else
	ndev = alloc_netdev_mq(sizeof(moal_private), name,
			       woal_vlan_virt_if_setup, 1);
#endif
#else
	ndev = alloc_netdev_mq(sizeof(moal_private), name,
			       woal_vlan_virt_if_setup);
#endif
	if (!ndev) {
		PRINTM(MFATAL, "Init virtual ethernet device failed\n");
		ret = -EFAULT;
		goto fail;
	}

	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0) {
		PRINTM(MFATAL, "Net device alloc name fail.\n");
		ret = -EFAULT;
		goto fail;
	}

	dev_net_set(ndev, wiphy_net(wiphy));

	moal_memcpy_ext(handle, ndev->perm_addr, wiphy->perm_addr, ETH_ALEN,
			sizeof(ndev->perm_addr));
	moal_memcpy_ext(handle, ndev->perm_addr, priv->current_addr, ETH_ALEN,
			sizeof(ndev->perm_addr));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	eth_hw_addr_set(ndev, priv->current_addr);
#else
	moal_memcpy_ext(handle, (t_void *)ndev->dev_addr, ndev->perm_addr,
			ETH_ALEN, MAX_ADDR_LEN);
#endif

	SET_NETDEV_DEV(ndev, wiphy_dev(wiphy));
	ndev->watchdog_timeo = MRVDRV_DEFAULT_UAP_WATCHDOG_TIMEOUT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	ndev->needed_headroom += MLAN_MIN_DATA_HEADER_LEN +
				 sizeof(mlan_buffer) + priv->extra_tx_head_len;
#else
	ndev->hard_header_len += MLAN_MIN_DATA_HEADER_LEN +
				 sizeof(mlan_buffer) + priv->extra_tx_head_len;
#endif

	ndev->flags |= IFF_BROADCAST | IFF_MULTICAST;

	new_priv = netdev_priv(ndev);

	ndev->ieee80211_ptr = &new_priv->w_dev;

	new_priv->wdev = &new_priv->w_dev;
	new_priv->netdev = ndev;
	new_priv->extra_tx_head_len = priv->extra_tx_head_len;

	moal_memcpy_ext(priv->phandle, new_priv->current_addr,
			priv->current_addr, ETH_ALEN, ETH_ALEN);

	new_priv->phandle = handle;
	new_priv->wdev->wiphy = handle->wiphy;
	new_priv->bss_type = MLAN_BSS_TYPE_UAP;
	new_priv->bss_role = MLAN_BSS_ROLE_UAP;
	new_priv->bss_index = priv->bss_index;
	new_priv->parent_priv = priv;
	new_priv->wdev->iftype = NL80211_IFTYPE_AP_VLAN;
	new_priv->max_tx_pending = MAX_TX_PENDING;
	new_priv->low_tx_pending = LOW_TX_PENDING;
	skb_queue_head_init(&new_priv->tx_q);

	ndev->ieee80211_ptr->use_4addr = params->use_4addr;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	ret = cfg80211_register_netdevice(ndev);
#else
	ret = register_netdevice(ndev);
#endif
	if (ret) {
		PRINTM(MFATAL, "register net_device failed, ret=%d\n", ret);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		cfg80211_unregister_netdevice(ndev);
#else
		unregister_netdevice(ndev);
#endif
		free_netdev(ndev);
		goto fail;
	}

	if (new_dev)
		*new_dev = ndev;

	if (ndev->ieee80211_ptr->use_4addr && !priv->multi_ap_flag) {
		/* Supports backhaul and fronthaul BSS and enable four_address
		 * flag */
		if (MLAN_STATUS_SUCCESS ==
		    woal_multi_ap_cfg(priv, MOAL_IOCTL_WAIT,
				      EASY_MESH_MULTI_AP_BH_AND_FH_BSS)) {
			priv->multi_ap_flag = EASY_MESH_MULTI_AP_BH_AND_FH_BSS;
		}
	}

fail:
	LEAVE();
	return ret;
}

#ifdef WIFI_DIRECT_SUPPORT
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
/**
 * @brief Callback function for virtual interface
 *      setup
 *
 *  @param dev    A pointer to structure net_device
 *
 *  @return       N/A
 */
static void woal_virt_if_setup(struct net_device *dev)
{
	ENTER();
	ether_setup(dev);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 11, 9)
	dev->needs_free_netdev = true;
#else
	dev->destructor = free_netdev;
#endif
	LEAVE();
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/**
 * @brief This function adds a new interface. It will
 *        allocate, initialize and register the device.
 *
 *  @param handle           A pointer to moal_handle structure
 *  @param bss_index        BSS index number
 *  @param name_assign_type Interface name assignment type
 *  @param bss_type         BSS type
 *
 *  @return                 A pointer to the new priv structure
 */
static moal_private *woal_alloc_virt_interface(moal_handle *handle,
					       t_u8 bss_index,
					       unsigned char name_assign_type,
					       t_u8 bss_type, const char *name)
#else
/**
 * @brief This function adds a new interface. It will
 *        allocate, initialize and register the device.
 *
 *  @param handle    A pointer to moal_handle structure
 *  @param bss_index BSS index number
 *  @param bss_type  BSS type
 *
 *  @return          A pointer to the new priv structure
 */
moal_private *woal_alloc_virt_interface(moal_handle *handle, t_u8 bss_index,
					t_u8 bss_type,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
					const
#endif
					char *name)
#endif
{
	struct net_device *dev = NULL;
	moal_private *priv = NULL;
	ENTER();

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#ifndef MAX_WMM_QUEUE
#define MAX_WMM_QUEUE 4
#endif
	/* Allocate an Ethernet device */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	dev = alloc_netdev_mq(sizeof(moal_private), name, name_assign_type,
			      woal_virt_if_setup, MAX_WMM_QUEUE);
#else
	dev = alloc_netdev_mq(sizeof(moal_private), name, NET_NAME_UNKNOWN,
			      woal_virt_if_setup, MAX_WMM_QUEUE);
#endif
#else
	dev = alloc_netdev_mq(sizeof(moal_private), name, woal_virt_if_setup,
			      MAX_WMM_QUEUE);
#endif
#else
	dev = alloc_netdev(sizeof(moal_private), name, woal_virt_if_setup);
#endif
	if (!dev) {
		PRINTM(MFATAL, "Init virtual ethernet device failed\n");
		goto error;
	}
	/* Allocate device name */
	if ((dev_alloc_name(dev, name) < 0)) {
		PRINTM(MERROR, "Could not allocate device name\n");
		goto error;
	}

	priv = (moal_private *)netdev_priv(dev);
	/* Save the priv to handle */
	handle->priv[bss_index] = priv;

	/* Use the same handle structure */
	priv->phandle = handle;
	priv->netdev = dev;
	priv->bss_index = bss_index;
	priv->bss_type = bss_type;
	priv->bss_role = MLAN_BSS_ROLE_STA;

	INIT_LIST_HEAD(&priv->tcp_sess_queue);
	spin_lock_init(&priv->tcp_sess_lock);

	INIT_LIST_HEAD(&priv->tx_stat_queue);
	spin_lock_init(&priv->tx_stat_lock);
	INIT_LIST_HEAD(&priv->mcast_list);
	spin_lock_init(&priv->mcast_lock);

#ifdef STA_CFG80211
	INIT_LIST_HEAD(&priv->dhcp_discover_queue);
	spin_lock_init(&priv->dhcp_discover_lock);
#endif

#ifdef STA_CFG80211
	INIT_LIST_HEAD(&priv->ipv6_addrses);
	spin_lock_init(&priv->ipv6addr_lock);
#endif
	spin_lock_init(&priv->connect_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
	SET_MODULE_OWNER(dev);
#endif

	PRINTM(MCMND, "Alloc virtual interface%s\n", dev->name);

	LEAVE();
	return priv;
error:
	if (dev)
		free_netdev(dev);
	LEAVE();
	return NULL;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy             A pointer to wiphy structure
 * @param name              Virtual interface name
 * @param name_assign_type  Interface name assignment type
 * @param type              Virtual interface type
 * @param flags             Flags for the virtual interface
 * @param params            A pointer to vif_params structure
 * @param new_dev		    new net_device to return
 *
 * @return                  0 -- success, otherwise fail
 */
static int woal_cfg80211_add_virt_if(struct wiphy *wiphy, const char *name,
				     unsigned char name_assign_type,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params,
				     struct net_device **new_dev)
#else
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param type            Virtual interface type
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 * @param new_dev		  new net_device to return
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_add_virt_if(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
			      const
#endif
			      char *name,
			      enum nl80211_iftype type, u32 *flags,
			      struct vif_params *params,
			      struct net_device **new_dev)
#endif
{
	int ret = 0;
	struct net_device *ndev = NULL;
	moal_private *priv = NULL, *new_priv = NULL;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	struct wireless_dev *wdev = NULL;
	moal_private *vir_priv;
	int i = 0;

	ENTER();
	ASSERT_RTNL();
	priv = woal_get_vir_priv_bss_type(handle, MLAN_BSS_TYPE_WIFIDIRECT);
	if (priv && priv->bss_role == MLAN_BSS_ROLE_UAP &&
	    priv->bss_started == MTRUE) {
		if (handle->pref_mac)
			handle = (moal_handle *)handle->pref_mac;
	}
	priv = (moal_private *)woal_get_priv_bss_type(handle,
						      MLAN_BSS_TYPE_WIFIDIRECT);
	if (!priv || !priv->phandle) {
		PRINTM(MERROR, "priv or handle is NULL\n");
		LEAVE();
		return -EFAULT;
	}
	if (priv->phandle->drv_mode.intf_num == priv->phandle->priv_num) {
		PRINTM(MERROR, "max virtual interface limit reached\n");
		for (i = 0; i < priv->phandle->priv_num; i++) {
			vir_priv = priv->phandle->priv[i];
			if (vir_priv->bss_virtual) {
				woal_cfg80211_del_virt_if(wiphy,
							  vir_priv->netdev);
				break;
			}
		}
		if (priv->phandle->drv_mode.intf_num ==
		    priv->phandle->priv_num) {
			LEAVE();
			return -ENOMEM;
		}
	}
	PRINTM(MMSG, "Add virtual interface %s\n", name);
	if ((type != NL80211_IFTYPE_P2P_CLIENT) &&
	    (type != NL80211_IFTYPE_P2P_GO)) {
		PRINTM(MERROR, "Invalid iftype: %d\n", type);
		LEAVE();
		return -EINVAL;
	}

	handle = priv->phandle;
	/* Cancel previous scan req */
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	new_priv = woal_alloc_virt_interface(handle, handle->priv_num,
					     name_assign_type,
					     MLAN_BSS_TYPE_WIFIDIRECT, name);
#else
	new_priv = woal_alloc_virt_interface(handle, handle->priv_num,
					     MLAN_BSS_TYPE_WIFIDIRECT, name);
#endif
	if (!new_priv) {
		PRINTM(MERROR, "Add virtual interface fail.");
		LEAVE();
		return -EFAULT;
	}
	handle->priv_num++;

	wdev = (struct wireless_dev *)&new_priv->w_dev;
	memset(wdev, 0, sizeof(struct wireless_dev));
	ndev = new_priv->netdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wiphy));
	ndev->ieee80211_ptr = wdev;
	wdev->iftype = type;
	wdev->wiphy = wiphy;
	new_priv->wdev = wdev;
	new_priv->bss_virtual = MTRUE;
	new_priv->pa_netdev = priv->netdev;

	/* Create workqueue for main process */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	new_priv->mclist_workqueue =
		alloc_workqueue("MCLIST_WORK_QUEUE",
				WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
#else
	new_priv->mclist_workqueue = create_workqueue("MCLIST_WORK_QUEUE");
#endif
	if (!new_priv->mclist_workqueue) {
		PRINTM(MERROR, "cannot alloc mclist workqueue \n");
		return -EFAULT;
	}
	MLAN_INIT_WORK(&new_priv->mclist_work, woal_mclist_work_queue);

	woal_init_sta_dev(ndev, new_priv);

	/* Initialize priv structure */
	woal_init_priv(new_priv, MOAL_IOCTL_WAIT);
	/** Init to GO/CLIENT mode */
	if (type == NL80211_IFTYPE_P2P_CLIENT) {
		if (woal_cfg80211_init_p2p_client(new_priv))
			PRINTM(MERROR, "%s: Fail to init p2p go", __func__);
	} else if (type == NL80211_IFTYPE_P2P_GO) {
		if (woal_cfg80211_init_p2p_go(new_priv))
			PRINTM(MERROR, "%s: Fail to init p2p client", __func__);
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	ret = cfg80211_register_netdevice(ndev);
#else
	ret = register_netdevice(ndev);
#endif
	if (ret) {
		handle->priv[new_priv->bss_index] = NULL;
		handle->priv_num--;
		if (ndev->reg_state == NETREG_REGISTERED) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
			cfg80211_unregister_netdevice(ndev);
#else
			unregister_netdevice(ndev);
#endif
			free_netdev(ndev);
			ndev = NULL;
		}
		PRINTM(MFATAL, "register net_device failed, ret=%d\n", ret);
		goto done;
	}
	netif_carrier_off(ndev);
	woal_stop_queue(ndev);
	if (new_dev)
		*new_dev = ndev;
#ifdef CONFIG_PROC_FS
	woal_create_proc_entry(new_priv);
	woal_debug_entry(new_priv);
#endif /* CONFIG_PROC_FS */
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Notify mlan BSS will be removed.
 *
 *  @param priv          A pointer to moal_private structure
 *
 *  @return              MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success,
 * otherwise fail
 */
static mlan_status woal_bss_remove(moal_private *priv)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_bss *bss = NULL;
	mlan_status status;

	ENTER();

	/* Allocate an IOCTL request buffer */
	req = (mlan_ioctl_req *)woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		status = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Fill request buffer */
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_BSS_REMOVE;
	req->req_id = MLAN_IOCTL_BSS;
	req->action = MLAN_ACT_SET;
	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief This function removes an virtual interface.
 *
 *  @param wiphy    A pointer to the wiphy structure
 *  @param dev      A pointer to the net_device structure
 *
 *  @return         0 -- success, otherwise fail
 */
int woal_cfg80211_del_virt_if(struct wiphy *wiphy, struct net_device *dev)
{
	int ret = 0;
	int i = 0;
	moal_private *priv = NULL;
	moal_private *vir_priv = NULL;
	moal_private *remain_priv = NULL;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	t_u8 find_bss = MFALSE;

	for (i = 0; i < handle->priv_num; i++) {
		vir_priv = handle->priv[i];
		if (vir_priv) {
			if (vir_priv->netdev == dev) {
				find_bss = MTRUE;
				PRINTM(MMSG,
				       "Del virtual interface %s, index=%d\n",
				       dev->name, i);
				break;
			}
		}
	}
	if (!find_bss) {
		/* Switch to the other MAC */
		if (handle->pref_mac)
			handle = (moal_handle *)handle->pref_mac;
		for (i = 0; i < handle->priv_num; i++) {
			vir_priv = handle->priv[i];
			if (vir_priv) {
				if (vir_priv->netdev == dev) {
					find_bss = MTRUE;
					PRINTM(MMSG,
					       "Del virtual interface %s, index=%d\n",
					       dev->name, i);
					break;
				}
			}
		}
	}

	priv = (moal_private *)woal_get_priv_bss_type(handle,
						      MLAN_BSS_TYPE_WIFIDIRECT);
	if (!priv)
		return ret;
	if (vir_priv && vir_priv->netdev == dev) {
		woal_stop_queue(dev);
		netif_carrier_off(dev);
		netif_device_detach(dev);
		if (handle->is_remain_timer_set) {
			woal_cancel_timer(&handle->remain_timer);
			woal_remain_timer_func(handle);
		}

		/*** cancel pending scan */
		woal_cancel_scan(vir_priv, MOAL_IOCTL_WAIT);

		woal_flush_tx_stat_queue(vir_priv);
		woal_flush_mcast_list(vir_priv);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		/* cancel previous remain on channel to avoid firmware hang */
		if (priv->phandle->remain_on_channel) {
			t_u8 channel_status;
			remain_priv =
				priv->phandle
					->priv[priv->phandle->remain_bss_index];
			if (remain_priv) {
				if (woal_cfg80211_remain_on_channel_cfg(
					    remain_priv, MOAL_IOCTL_WAIT, MTRUE,
					    &channel_status, NULL, 0, 0))
					PRINTM(MERROR,
					       "del_virt_if: Fail to cancel remain on channel\n");

				if (priv->phandle->cookie) {
					cfg80211_remain_on_channel_expired(
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
						remain_priv->netdev,
#else
						remain_priv->wdev,
#endif
						priv->phandle->cookie,
						&priv->phandle->chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
						priv->phandle->channel_type,
#endif
						GFP_ATOMIC);
					priv->phandle->cookie = 0;
				}
				priv->phandle->remain_on_channel = MFALSE;
			}
		}
#endif
		if (vir_priv->mclist_workqueue) {
			flush_workqueue(vir_priv->mclist_workqueue);
			destroy_workqueue(vir_priv->mclist_workqueue);
			vir_priv->mclist_workqueue = NULL;
		}
		woal_clear_all_mgmt_ies(vir_priv, MOAL_IOCTL_WAIT);
		woal_cfg80211_deinit_p2p(vir_priv);
		woal_bss_remove(vir_priv);
#ifdef CONFIG_PROC_FS
		/* Remove proc debug */
		woal_debug_remove(vir_priv);
		woal_proc_remove(vir_priv);
#endif /* CONFIG_PROC_FS */
		/* Last reference is our one */
#if CFG80211_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
		PRINTM(MINFO, "refcnt = %d\n", atomic_read(&dev->refcnt));
#else
		PRINTM(MINFO, "refcnt = %d\n", netdev_refcnt_read(dev));
#endif
		PRINTM(MINFO, "netdev_finish_unregister: %s\n", dev->name);
		/* Clear the priv in handle */
		vir_priv->phandle->priv[vir_priv->bss_index] = NULL;
		priv->phandle->priv_num--;
		if (dev->reg_state == NETREG_REGISTERED)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
			cfg80211_unregister_netdevice(dev);
#else
			unregister_netdevice(dev);
#endif
	}
	return ret;
}
#endif
#endif

/**
 *  @brief This function removes an virtual interface.
 *
 *  @param handle    A pointer to the moal_handle structure
 *
 *  @return        N/A
 */
void woal_remove_virtual_interface(moal_handle *handle)
{
#ifdef WIFI_DIRECT_SUPPORT
	moal_private *priv = NULL;
	int vir_intf = 0;
	int i = 0;
	moal_handle *ref_handle = NULL;
	int ref_vir_intf = 0;
#endif
	ENTER();
	rtnl_lock();
#ifdef WIFI_DIRECT_SUPPORT
	for (i = 0; i < handle->priv_num; i++) {
		priv = handle->priv[i];
		if (priv) {
			if (priv->bss_virtual) {
				PRINTM(MCMND, "Remove virtual interface %s\n",
				       priv->netdev->name);
#ifdef CONFIG_PROC_FS
				/* Remove proc debug */
				woal_debug_remove(priv);
				woal_proc_remove(priv);
#endif /* CONFIG_PROC_FS */
				netif_device_detach(priv->netdev);
				if (priv->netdev->reg_state ==
				    NETREG_REGISTERED)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
					cfg80211_unregister_netdevice(
						priv->netdev);
#else
					unregister_netdevice(priv->netdev);
#endif
				handle->priv[i] = NULL;
				vir_intf++;
			}
		}
	}
	if (handle->pref_mac) {
		ref_handle = (moal_handle *)handle->pref_mac;
		for (i = 0; i < ref_handle->priv_num; i++) {
			priv = ref_handle->priv[i];
			if (priv) {
				if (priv->bss_virtual &&
				    priv->wdev->wiphy == handle->wiphy) {
					PRINTM(MCMND,
					       "Remove virtual interfaces from pref mac %s\n",
					       priv->netdev->name);
#ifdef CONFIG_PROC_FS
					/* Remove proc debug */
					woal_debug_remove(priv);
					woal_proc_remove(priv);
#endif /* CONFIG_PROC_FS */
					netif_device_detach(priv->netdev);
					if (priv->netdev->reg_state ==
					    NETREG_REGISTERED)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
						cfg80211_unregister_netdevice(
							priv->netdev);
#else
						unregister_netdevice(
							priv->netdev);
#endif
					ref_handle->priv[i] = NULL;
					ref_vir_intf++;
				}
			}
		}
		ref_handle->priv_num -= ref_vir_intf;
	}
#endif
	if (handle->mon_if) {
		netif_device_detach(handle->mon_if->mon_ndev);
		if (handle->mon_if->mon_ndev->reg_state == NETREG_REGISTERED)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
			cfg80211_unregister_netdevice(handle->mon_if->mon_ndev);
#else
			unregister_netdevice(handle->mon_if->mon_ndev);
#endif
		handle->mon_if = NULL;
	}
	rtnl_unlock();
#ifdef WIFI_DIRECT_SUPPORT
	handle->priv_num -= vir_intf;
#endif
	LEAVE();
}

/**
 *  @brief This function check if uap interface is ready
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 *
 *  @return        MTRUE/MFALSE;
 */
static t_u8 woal_uap_interface_ready(struct wiphy *wiphy, char *name,
				     struct net_device **new_dev)
{
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv = NULL;
	int i;

	for (i = 0; i < handle->priv_num; i++) {
		priv = handle->priv[i];
		if (priv && (priv->bss_type == MLAN_BSS_TYPE_UAP) &&
		    !strcmp(priv->netdev->name, name)) {
			priv->wdev->iftype = NL80211_IFTYPE_AP;
			*new_dev = priv->netdev;
			break;
		}
	}
	if (priv && *new_dev)
		return MTRUE;
	else
		return MFALSE;
}

#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 37)
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param type            Virtual interface type
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 *
 * @return                A pointer to net_device -- success, otherwise null
 */
struct net_device *woal_cfg80211_add_virtual_intf(struct wiphy *wiphy,
						  char *name,
						  enum nl80211_iftype type,
						  u32 *flags,
						  struct vif_params *params)
#else
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param type            Virtual interface type
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_add_virtual_intf(struct wiphy *wiphy, char *name,
				   enum nl80211_iftype type, u32 *flags,
				   struct vif_params *params)
#endif
#else
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param name            Virtual interface name
 * @param type            Virtual interface type
 * @param flags           Flags for the virtual interface
 * @param params          A pointer to vif_params structure
 *
 * @return                A pointer to wireless_dev -- success, otherwise null
 */
struct wireless_dev *woal_cfg80211_add_virtual_intf(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
						    const
#endif
						    char *name,
						    enum nl80211_iftype type,
						    u32 *flags,
						    struct vif_params *params)
#else
/**
 * @brief Request the driver to add a virtual interface
 *
 * @param wiphy             A pointer to wiphy structure
 * @param name              Virtual interface name
 * @param name_assign_type  Interface name assignment type
 * @param type              Virtual interface type
 * @param flags             Flags for the virtual interface
 * @param params            A pointer to vif_params structure
 *
 * @return                  A pointer to wireless_dev -- success, otherwise null
 */
struct wireless_dev *
woal_cfg80211_add_virtual_intf(struct wiphy *wiphy, const char *name,
			       unsigned char name_assign_type,
			       enum nl80211_iftype type,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
			       u32 *flags,
#endif
			       struct vif_params *params)
#endif
#endif
{
	struct net_device *ndev = NULL;
	int ret = 0;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
	u32 *flags = &params->flags;
#endif

	ENTER();
	PRINTM(MIOCTL, "add virtual intf: %d name: %s\n", type, name);
	switch (type) {
	case NL80211_IFTYPE_MONITOR:
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
		ret = woal_cfg80211_add_mon_if(wiphy, name, name_assign_type,
					       flags, params, &ndev);
#else
		ret = woal_cfg80211_add_mon_if(wiphy, name, flags, params,
					       &ndev);
#endif
		break;
#ifdef WIFI_DIRECT_SUPPORT
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
		ret = woal_cfg80211_add_virt_if(wiphy, name, name_assign_type,
						type, flags, params, &ndev);
#else
		ret = woal_cfg80211_add_virt_if(wiphy, name, type, flags,
						params, &ndev);
#endif
		break;
#endif
#endif
	case NL80211_IFTYPE_AP:
		if (!woal_uap_interface_ready(wiphy, (char *)name, &ndev)) {
			PRINTM(MMSG,
			       "Not support dynamically create %s UAP interface\n",
			       name);
			ret = -EFAULT;
		}
		break;
	case NL80211_IFTYPE_AP_VLAN:
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
		ret = woal_cfg80211_add_vlan_vir_if(
			wiphy, name, name_assign_type, flags, params, &ndev);
#else
		ret = woal_cfg80211_add_vlan_vir_if(
			wiphy, name, name_assign_type, params, &ndev);
#endif
#else
		ret = woal_cfg80211_add_vlan_vir_if(wiphy, name, flags, params,
						    &ndev);
#endif
		break;
	default:
		PRINTM(MWARN, "Not supported if type: %d\n", type);
		ret = -EFAULT;
		break;
	}
	LEAVE();
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 37)
	if (ret)
		return ERR_PTR(ret);
	else
		return ndev;
#else
	return ret;
#endif
#else
	if (ret)
		return ERR_PTR(ret);
	else
		return ndev->ieee80211_ptr;
#endif
}

#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
/**
 * @brief Request the driver to del a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             The pointer to net_device
 *
 * @return               0 -- success, otherwise fail
 */
int woal_cfg80211_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
#else
/**
 * @brief Request the driver to del a virtual interface
 *
 * @param wiphy           A pointer to wiphy structure
 * @param wdev            The pointer to wireless_dev
 *
 * @return               0 -- success, otherwise fail
 */
int woal_cfg80211_del_virtual_intf(struct wiphy *wiphy,
				   struct wireless_dev *wdev)
#endif
{
	int ret = 0;

	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	int i;
	moal_private *vir_priv = NULL;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *dev = wdev->netdev;
#endif

	moal_private *vlan_priv = NULL;
	station_node *vlan_sta_list = NULL;
	t_u16 aid = 0;

	ENTER();

	PRINTM(MIOCTL, "del virtual intf %s\n", dev->name);
	ASSERT_RTNL();
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_MONITOR) {
		if ((handle->mon_if) && (handle->mon_if->mon_ndev == dev)) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_net_monitor(handle->mon_if->priv,
						 MOAL_IOCTL_WAIT, MFALSE, 0,
						 NULL)) {
				PRINTM(MERROR,
				       "%s: woal_set_net_monitor fail\n",
				       __func__);
				ret = -EFAULT;
			}
#endif
			handle->mon_if = NULL;
		}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		cfg80211_unregister_netdevice(dev);
#else
		unregister_netdevice(dev);
#endif
		LEAVE();
		return ret;
	}

#ifdef UAP_SUPPORT
	/**
	 * For multi-ap virtual interface, unregister netdevice
	 * directly for now. Will add more in the future.
	 */
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP_VLAN) {
		/* stop network before doing cleanup */
		if (netif_carrier_ok(dev))
			netif_carrier_off(dev);
		vlan_priv = (moal_private *)netdev_priv(dev);
		aid = vlan_priv->vlan_sta_ptr->aid;
		PRINTM(MCMND, "wlan: Easymesh del Vlan aid=%d\n", aid);
		vlan_sta_list =
			vlan_priv->parent_priv
				->vlan_sta_list[(aid - 1) % MAX_STA_COUNT];
		if (vlan_sta_list)
			vlan_sta_list->is_valid = MFALSE;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		cfg80211_unregister_netdevice(dev);
#else
		unregister_netdevice(dev);
#endif
		LEAVE();
		return ret;
	}
#endif

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		for (i = 0; i < handle->priv_num; i++) {
			vir_priv = handle->priv[i];
			if (vir_priv) {
				if (vir_priv->netdev == dev) {
					PRINTM(MMSG,
					       "Del virtual interface %s, index=%d\n",
					       dev->name, i);
					break;
				}
			}
		}
		if (vir_priv && vir_priv->bss_type == MLAN_BSS_TYPE_UAP) {
#if ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                    \
     (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
			if (woal_cfg80211_del_beacon(wiphy, dev, 0))
#else
			if (woal_cfg80211_del_beacon(wiphy, dev))
#endif
				PRINTM(MERROR, "%s: del_beacon failed\n",
				       __func__);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
#if ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                    \
     (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
			vir_priv->wdev->links[0].ap.beacon_interval = 0;
#else
			vir_priv->wdev->beacon_interval = 0;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
#if ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                    \
     (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
			memset(&vir_priv->wdev->links[0].ap.chandef, 0,
			       sizeof(vir_priv->wdev->links[0].ap.chandef));
#else
			memset(&vir_priv->wdev->chandef, 0,
			       sizeof(vir_priv->wdev->chandef));
#endif
#endif
#endif
#if ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                    \
     (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
			vir_priv->wdev->u.ap.ssid_len = 0;
#else
			vir_priv->wdev->ssid_len = 0;
#endif
			PRINTM(MMSG, "Skip del UAP virtual interface %s",
			       dev->name);
		}
		LEAVE();
		return ret;
	}

#ifdef WIFI_DIRECT_SUPPORT
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	ret = woal_cfg80211_del_virt_if(wiphy, dev);
#endif
#endif
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
/**
 * @brief initialize AP or GO parameters

 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to cfg80211_ap_settings structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_add_beacon(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ap_settings *params)
#else
/**
 * @brief initialize AP or GO parameters

 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to beacon_parameters structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_add_beacon(struct wiphy *wiphy, struct net_device *dev,
			     struct beacon_parameters *params)
#endif
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;

	t_u8 wait_option = MOAL_IOCTL_WAIT_TIMEOUT;

	ENTER();

	PRINTM(MMSG, "wlan: %s Starting AP\n", dev->name);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	/* cancel previous remain on channel to avoid firmware hang */
	if (priv->phandle->remain_on_channel) {
		t_u8 channel_status;
		moal_private *remain_priv;
		remain_priv =
			priv->phandle->priv[priv->phandle->remain_bss_index];
		if (remain_priv) {
			PRINTM(MCMND,
			       "Cancel Remain on Channel before Starting AP\n");
			if (woal_cfg80211_remain_on_channel_cfg(
				    remain_priv, MOAL_IOCTL_WAIT, MTRUE,
				    &channel_status, NULL, 0, 0))
				PRINTM(MERROR,
				       "add beacon: Fail to cancel remain on channel\n");
			if (priv->phandle->cookie) {
				cfg80211_remain_on_channel_expired(
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
					remain_priv->netdev,
#else
					remain_priv->wdev,
#endif
					priv->phandle->cookie,
					&priv->phandle->chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
					priv->phandle->channel_type,
#endif
					GFP_ATOMIC);
				priv->phandle->cookie = 0;
			}
			priv->phandle->remain_on_channel = MFALSE;
		}
	}
#endif

#ifdef STA_CFG80211
	/*** cancel pending scan */
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);
#endif

	if (!params) {
		LEAVE();
		return -EFAULT;
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	priv->channel = ieee80211_frequency_to_channel(
		params->chandef.chan->center_freq);
	priv->bandwidth = params->chandef.width;
#else
	priv->channel =
		ieee80211_frequency_to_channel(params->channel->center_freq);
#endif
#endif
	/* bss config */
	if (MLAN_STATUS_SUCCESS != woal_cfg80211_beacon_config(priv, params)) {
		ret = -EFAULT;
		goto done;
	}

	/* set mgmt frame ies */
	ret = woal_cfg80211_mgmt_frame_ie(priv,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
					  params->tail, params->tail_len, NULL,
					  0, NULL, 0, NULL, 0, MGMT_MASK_BEACON
#else
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
					  params->beacon.tail,
					  params->beacon.tail_len,
					  params->beacon.proberesp_ies,
					  params->beacon.proberesp_ies_len,
					  params->beacon.assocresp_ies,
					  params->beacon.assocresp_ies_len,
#else
					  params->tail, params->tail_len,
					  params->proberesp_ies,
					  params->proberesp_ies_len,
					  params->assocresp_ies,
					  params->assocresp_ies_len,
#endif
					  NULL, 0,
					  MGMT_MASK_BEACON |
						  MGMT_MASK_PROBE_RESP |
						  MGMT_MASK_ASSOC_RESP
#endif
					  ,
					  MOAL_IOCTL_WAIT);
	if (ret)
		goto done;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	if (params->beacon.beacon_ies && params->beacon.beacon_ies_len) {
		ret = woal_cfg80211_mgmt_frame_ie(
			priv, params->beacon.beacon_ies,
			params->beacon.beacon_ies_len, NULL, 0, NULL, 0, NULL,
			0, MGMT_MASK_BEACON_WPS_P2P, MOAL_IOCTL_WAIT);
		if (ret) {
			PRINTM(MERROR, "Failed to set beacon wps/p2p ie\n");
			goto done;
		}
	}
#else
	if (params->beacon_ies && params->beacon_ies_len) {
		ret = woal_cfg80211_mgmt_frame_ie(priv, params->beacon_ies,
						  params->beacon_ies_len, NULL,
						  0, NULL, 0, NULL, 0,
						  MGMT_MASK_BEACON_WPS_P2P,
						  MOAL_IOCTL_WAIT);
		if (ret) {
			PRINTM(MERROR, "Failed to set beacon wps/p2p ie\n");
			goto done;
		}
	}
#endif
#endif
	priv->uap_host_based = MTRUE;

	/* if the bss is stopped, then start it */
	if (priv->bss_started == MFALSE) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		if (moal_extflg_isset(priv->phandle, EXT_DFS_OFFLOAD))
			wait_option = MOAL_NO_WAIT;
#endif
		if (woal_uap_bss_ctrl(priv, wait_option, UAP_BSS_START)) {
			PRINTM(MERROR, "%s: start uap failed \n", __func__);
			priv->uap_host_based = MFALSE;
			ret = -EFAULT;
			goto done;
		}
	}
	PRINTM(MMSG, "wlan: %s AP started\n", dev->name);
done:
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
/**
 * @brief set AP or GO parameter
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param info            A pointer to cfg80211_ap_update structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_beacon(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ap_update *info)
#elif CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
/**
 * @brief set AP or GO parameter
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to cfg80211_beacon_data structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_beacon(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_beacon_data *params)
#else
/**
 * @brief set AP or GO parameter
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to beacon_parameters structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_beacon(struct wiphy *wiphy, struct net_device *dev,
			     struct beacon_parameters *params)
#endif
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
	struct cfg80211_beacon_data *params = &info->beacon;
#endif
	int ret = 0;

	ENTER();

	PRINTM(MIOCTL, "set beacon\n");
	if (params != NULL) {
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
		if (params->tail && params->tail_len) {
			ret = woal_cfg80211_mgmt_frame_ie(
				priv, params->tail, params->tail_len, NULL, 0,
				NULL, 0, NULL, 0, MGMT_MASK_BEACON,
				MOAL_IOCTL_WAIT);
			if (ret)
				goto done;
		}
#else
		t_u16 mask = 0;
		if (params->tail && params->tail_len)
			mask |= MGMT_MASK_BEACON;
		if (params->proberesp_ies && params->proberesp_ies_len)
			mask |= MGMT_MASK_PROBE_RESP;
		if (params->assocresp_ies && params->assocresp_ies_len)
			mask |= MGMT_MASK_ASSOC_RESP;
		PRINTM(MIOCTL, "Set beacon: mask=0x%x\n", mask);
		if (mask) {
			ret = woal_cfg80211_mgmt_frame_ie(
				priv, params->tail, params->tail_len,
				params->proberesp_ies,
				params->proberesp_ies_len,
				params->assocresp_ies,
				params->assocresp_ies_len, NULL, 0, mask,
				MOAL_IOCTL_WAIT);
			if (ret)
				goto done;
		}
		if (params->beacon_ies && params->beacon_ies_len) {
			ret = woal_cfg80211_mgmt_frame_ie(
				priv, params->beacon_ies,
				params->beacon_ies_len, NULL, 0, NULL, 0, NULL,
				0, MGMT_MASK_BEACON_WPS_P2P, MOAL_IOCTL_WAIT);
			if (ret) {
				PRINTM(MERROR,
				       "Failed to set beacon wps/p2p ie\n");
				goto done;
			}
		}
#endif
	}

done:
	LEAVE();
	return ret;
}

/**
 * @brief reset AP or GO parameters
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
#if ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                    \
     (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
int woal_cfg80211_del_beacon(struct wiphy *wiphy, struct net_device *dev,
			     unsigned int link_id)
#else
int woal_cfg80211_del_beacon(struct wiphy *wiphy, struct net_device *dev)
#endif
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
#ifdef STA_SUPPORT
	moal_private *pmpriv = NULL;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
	moal_private *dfs_priv =
		woal_get_priv_bss_type(priv->phandle, MLAN_BSS_TYPE_DFS);
#endif

	ENTER();

	if (priv->phandle->driver_status || priv->phandle->surprise_removed) {
		PRINTM(MERROR,
		       "Block  woal_cfg80211_del_beacon in abnormal driver state\n");
		LEAVE();
		return ret;
	}
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	woal_update_uap_channel_dfs_state(priv);
#endif
#endif
	priv->uap_host_based = MFALSE;
	PRINTM(MMSG, "wlan: %s Stopping AP\n", dev->name);
#ifdef STA_SUPPORT
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);
#endif
	memset(priv->dscp_map, 0xFF, sizeof(priv->dscp_map));
	woal_deauth_all_station(priv);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (moal_extflg_isset(priv->phandle, EXT_DFS_OFFLOAD))
		woal_cancel_cac_block(priv);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
	if (dfs_priv && dfs_priv->radar_background) {
		PRINTM(MMSG, "Cancel background radar detection\n");
		woal_11h_cancel_chan_report_ioctl(dfs_priv, MOAL_IOCTL_WAIT);
		dfs_priv->chan_rpt_pending = MFALSE;
		dfs_priv->radar_background = MFALSE;
		woal_update_channels_dfs_state(
			dfs_priv, dfs_priv->chan_rpt_req.chanNum,
			dfs_priv->chan_rpt_req.bandcfg.chanWidth, DFS_USABLE);
		memset(&dfs_priv->chan_rpt_req, 0,
		       sizeof(mlan_ds_11h_chan_rep_req));
		cfg80211_background_cac_abort(priv->phandle->wiphy);
	}
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	memset(&priv->chan, 0, sizeof(struct cfg80211_chan_def));
	if (priv->phandle->is_cac_timer_set &&
	    priv->bss_index == priv->phandle->cac_bss_index) {
		woal_cancel_timer(&priv->phandle->cac_timer);
		priv->phandle->is_cac_timer_set = MFALSE;
		/* Make sure Chan Report is cancelled */
		if (woal_11h_cancel_chan_report_ioctl(priv, MOAL_IOCTL_WAIT))
			PRINTM(MERROR, "%s: cancel chan report failed \n",
			       __func__);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		cfg80211_cac_event(priv->netdev, &priv->phandle->dfs_channel,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL, 0);
#elif CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		cfg80211_cac_event(priv->netdev, &priv->phandle->dfs_channel,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
#else
		cfg80211_cac_event(priv->netdev, NL80211_RADAR_CAC_ABORTED,
				   GFP_KERNEL);
#endif
		memset(&priv->phandle->dfs_channel, 0,
		       sizeof(struct cfg80211_chan_def));
		priv->phandle->cac_bss_index = 0xff;
	}
	if (priv->csa_workqueue)
		flush_workqueue(priv->csa_workqueue);
#endif
	/* if the bss is still running, then stop it */
	if (priv->bss_started == MTRUE) {
		if (woal_uap_bss_ctrl(priv, MOAL_NO_WAIT, UAP_BSS_STOP)) {
			PRINTM(MERROR, "%s: stop uap failed \n", __func__);
			goto done;
		}
		if (woal_uap_bss_ctrl(priv, MOAL_NO_WAIT, UAP_BSS_RESET)) {
			PRINTM(MERROR, "%s: reset uap failed \n", __func__);
			goto done;
		}
		/* Set WLAN MAC addresses */
		if (MLAN_STATUS_FAILURE ==
		    woal_request_set_mac_address(priv, MOAL_NO_WAIT)) {
			PRINTM(MERROR, "Set MAC address failed\n");
			goto done;
		}
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		woal_cfg80211_setup_uap_he_cap(priv, MOAL_NO_WAIT);
#endif
	}
	woal_clear_all_mgmt_ies(priv, MOAL_NO_WAIT);
#ifdef STA_SUPPORT
	if (!woal_is_any_interface_active(priv->phandle)) {
		pmpriv = woal_get_priv((moal_handle *)priv->phandle,
				       MLAN_BSS_ROLE_STA);
		if (pmpriv && !priv->phandle->user_scan_cfg) {
			woal_set_scan_time(pmpriv, ACTIVE_SCAN_CHAN_TIME,
					   PASSIVE_SCAN_CHAN_TIME,
					   SPECIFIC_SCAN_CHAN_TIME);
		}
	}
#endif
	priv->cipher = 0;
	memset(priv->uap_wep_key, 0, sizeof(priv->uap_wep_key));
	priv->channel = 0;
	priv->bandwidth = 0;
	priv->multi_ap_flag = 0;

	PRINTM(MMSG, "wlan: %s AP stopped\n", dev->name);
done:
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 * @brief change BSS
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to bss_parameters structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev,
			     struct bss_parameters *params)
{
	int ret = 0;
	t_u8 change = MFALSE;
	mlan_uap_bss_param *sys_config = NULL;
	u8 bss_started = MFALSE;
	t_u8 pkt_forward_ctl;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();
	PRINTM(MIOCTL, "isolate=%d\n", params->ap_isolate);

	sys_config = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_config) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv, MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   sys_config)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

	pkt_forward_ctl = sys_config->pkt_forward_ctl;
	if (params->ap_isolate) {
		/** disable packet forwarding */
		sys_config->pkt_forward_ctl |= PKT_FWD_INTRA_BCAST;
		sys_config->pkt_forward_ctl |= PKT_FWD_INTRA_UCAST;
	} else {
		sys_config->pkt_forward_ctl &= ~PKT_FWD_INTRA_BCAST;
		sys_config->pkt_forward_ctl &= ~PKT_FWD_INTRA_UCAST;
	}
	if (pkt_forward_ctl != sys_config->pkt_forward_ctl) {
		change = MTRUE;
		PRINTM(MIOCTL, "ap_isolate=%xd\n", params->ap_isolate);
	}
	if (change) {
		if (priv->bss_started == MTRUE) {
			bss_started = MTRUE;
			if (woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
					      UAP_BSS_STOP))
				PRINTM(MERROR, "%s: stop uap failed \n",
				       __func__);
		}

		if (params->use_short_preamble == 1)
			sys_config->preamble_type = 1;
		else if (params->use_short_preamble == 0)
			sys_config->preamble_type = 2;
		else
			sys_config->preamble_type = 0;

		if (MLAN_STATUS_SUCCESS ==
		    woal_set_get_sys_config(priv, MLAN_ACT_SET, MOAL_IOCTL_WAIT,
					    sys_config))
			ret = 0;
		if (bss_started) {
			if (woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT_TIMEOUT,
					      UAP_BSS_START))
				PRINTM(MERROR, "%s: start uap failed \n",
				       __func__);
		}
	}
done:
	kfree(sys_config);
	LEAVE();
	return ret;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 * @brief del station
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param param           A pointer tostation_del_parameters structure
 *
 * @return                0 -- success, otherwise fail
 */
#else
/**
 * @brief del station
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param mac_addr		  A pointer to station mac address
 *
 * @return                0 -- success, otherwise fail
 */
#endif
int woal_cfg80211_del_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
			      struct station_del_parameters *param)
#else
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			      const u8 *mac_addr)
#else
			      u8 *mac_addr)
#endif
#endif
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	const u8 *mac_addr = NULL;
#endif
	u16 reason_code = REASON_CODE_DEAUTH_LEAVING;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
#ifdef UAP_SUPPORT
#if defined(UAP_CFG80211) || defined(STA_CFG80211)
	int i;
#endif
#endif
	ENTER();

#ifdef UAP_SUPPORT
	if ((priv->bss_type == MLAN_BSS_TYPE_UAP) && !priv->bss_started) {
		woal_cancel_cac(priv);
		LEAVE();
		return 0;
	}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	if (param) {
		mac_addr = param->mac;
		reason_code = param->reason_code;
	}
#endif

	/** we will not send deauth to p2p interface, it might cause WPS failure
	 */
	if (mac_addr
#ifdef WIFI_DIRECT_SUPPORT
	    && !priv->phandle->is_go_timer_set
#endif
	) {
		if (woal_deauth_assoc_station(priv, (u8 *)mac_addr,
					      reason_code))
			PRINTM(MMSG, "wlan: deauth station " MACSTR " failed\n",
			       MAC2STR(mac_addr));
	} else {
		PRINTM(MIOCTL, "del station\n");
	}

#ifdef UAP_SUPPORT
#if defined(UAP_CFG80211) || defined(STA_CFG80211)
	if (mac_addr) {
		for (i = 0; i < MAX_STA_COUNT; i++) {
			if (priv->vlan_sta_list[i] &&
			    !moal_memcmp(priv->phandle,
					 priv->vlan_sta_list[i]->peer_mac,
					 (u8 *)mac_addr,
					 MLAN_MAC_ADDR_LENGTH)) {
				kfree(priv->vlan_sta_list[i]);
				priv->vlan_sta_list[i] = NULL;
				break;
			}
		}
	}
#endif
#endif

	LEAVE();
	return 0;
}

/**
 * @brief Get station info
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param mac			  A pointer to station mac address
 * @param stainfo		  A pointer to station_info structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_uap_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
				  const u8 *mac,
#else
				  u8 *mac,
#endif
				  struct station_info *stainfo)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = -EFAULT;
	int i = 0;
	mlan_ds_get_info *info = NULL;
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_get_stats stats;

	ENTER();
	if (priv->media_connected == MFALSE) {
		PRINTM(MINFO, "cfg80211: Media not connected!\n");
		LEAVE();
		return -ENOENT;
	}

	/* Allocate an IOCTL request buffer */
	ioctl_req = (mlan_ioctl_req *)woal_alloc_mlan_ioctl_req(
		sizeof(mlan_ds_get_info) +
		(MAX_STA_LIST_IE_SIZE * MAX_NUM_CLIENTS));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	info = (mlan_ds_get_info *)ioctl_req->pbuf;
	info->sub_command = MLAN_OID_UAP_STA_LIST;
	ioctl_req->req_id = MLAN_IOCTL_GET_INFO;
	ioctl_req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS)
		goto done;
	for (i = 0; i < info->param.sta_list.sta_count; i++) {
		if (!memcmp(info->param.sta_list.info[i].mac_address, mac,
			    ETH_ALEN)) {
			PRINTM(MIOCTL, "Get station: " MACSTR " RSSI=%d\n",
			       MAC2STR(mac),
			       (int)info->param.sta_list.info[i].rssi);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
			stainfo->filled = BIT(NL80211_STA_INFO_INACTIVE_TIME) |
					  BIT(NL80211_STA_INFO_RX_BYTES) |
					  BIT(NL80211_STA_INFO_TX_BYTES) |
					  BIT(NL80211_STA_INFO_RX_PACKETS) |
					  BIT(NL80211_STA_INFO_TX_PACKETS) |
					  BIT(NL80211_STA_INFO_SIGNAL);
			stainfo->rx_bytes =
				info->param.sta_list.info[i].stats.rx_bytes;
			stainfo->tx_bytes =
				info->param.sta_list.info[i].stats.tx_bytes;
			stainfo->rx_packets =
				info->param.sta_list.info[i].stats.rx_packets;
			stainfo->tx_packets =
				info->param.sta_list.info[i].stats.tx_packets;
#else
			stainfo->filled = STATION_INFO_INACTIVE_TIME |
					  STATION_INFO_SIGNAL;
#endif
			stainfo->inactive_time = 0;
			stainfo->signal = info->param.sta_list.info[i].rssi;
			ret = 0;
			break;
		}
	}
	memset(&stats, 0, sizeof(mlan_ds_get_stats));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_stats_info(priv, MOAL_IOCTL_WAIT, &stats)) {
		PRINTM(MERROR, "Error getting stats information\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	stainfo->filled |= BIT(NL80211_STA_INFO_TX_RETRIES) |
			   BIT(NL80211_STA_INFO_TX_FAILED) |
			   BIT(NL80211_STA_INFO_RX_DROP_MISC);
	stainfo->tx_failed = stats.failed;
	stainfo->tx_retries = stats.retry;
	stainfo->rx_dropped_misc = stats.fcs_error;
#endif
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to dump the station information
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param idx             Station index
 * @param mac             MAC address of the station
 * @param sinfo           A pointer to station_info structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_uap_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
				   int idx, t_u8 *mac,
				   struct station_info *sinfo)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = -EFAULT;
	mlan_ds_get_info *info = NULL;
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	t_u32 sec = 0, usec = 0;
	t_u64 cur_msec = 0;

	ENTER();
	if (priv->media_connected == MFALSE) {
		PRINTM(MINFO, "cfg80211: Media not connected!\n");
		LEAVE();
		return -ENOENT;
	}

	/* Allocate an IOCTL request buffer */
	ioctl_req = (mlan_ioctl_req *)woal_alloc_mlan_ioctl_req(
		sizeof(mlan_ds_get_info) +
		(MAX_STA_LIST_IE_SIZE * MAX_NUM_CLIENTS));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	info = (mlan_ds_get_info *)ioctl_req->pbuf;
	info->sub_command = MLAN_OID_UAP_STA_LIST;
	ioctl_req->req_id = MLAN_IOCTL_GET_INFO;
	ioctl_req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS)
		goto done;
	if (idx >= info->param.sta_list.sta_count) {
		ret = -EFAULT;
		goto done;
	}
	ret = 0;
	moal_memcpy_ext(priv->phandle, mac,
			info->param.sta_list.info[idx].mac_address, ETH_ALEN,
			ETH_ALEN);
	PRINTM(MIOCTL, "Dump station: " MACSTR " RSSI=%d\n", MAC2STR(mac),
	       (int)info->param.sta_list.info[idx].rssi);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	sinfo->filled = BIT(NL80211_STA_INFO_INACTIVE_TIME) |
			BIT(NL80211_STA_INFO_SIGNAL);
#else
	sinfo->filled = STATION_INFO_INACTIVE_TIME | STATION_INFO_SIGNAL;
#endif
	if (info->param.sta_list.info[idx].stats.last_rx_in_msec) {
		moal_get_system_time(priv->phandle, &sec, &usec);
		cur_msec = (t_u64)sec * 1000 + (t_u64)usec / 1000;
		sinfo->inactive_time =
			(t_u32)(cur_msec - info->param.sta_list.info[idx]
						   .stats.last_rx_in_msec);
		PRINTM(MIOCTL,
		       "cur:%llu - [%d].last_rx:%llu = inactive_time:%d\n",
		       cur_msec, idx,
		       info->param.sta_list.info[idx].stats.last_rx_in_msec,
		       sinfo->inactive_time);
	} else
		sinfo->inactive_time = 0;
	sinfo->signal = info->param.sta_list.info[idx].rssi;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
/**
 * @brief set mac filter
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params		  A pointer to cfg80211_acl_data structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_mac_acl(struct wiphy *wiphy, struct net_device *dev,
			      const struct cfg80211_acl_data *params)
{
	int ret = -EFAULT;
	mlan_uap_bss_param *sys_config = NULL;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	u8 bss_started = MFALSE;
	ENTER();

	PRINTM(MIOCTL, "Set mac acl, entries=%d, policy=%d\n",
	       params->n_acl_entries, params->acl_policy);
	sys_config = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_config) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}

	/* Initialize the uap bss values which are uploaded from firmware */
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv, MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   sys_config)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}
	memset(&sys_config->filter, 0, sizeof(mac_filter));
	if (params->n_acl_entries <= MAX_MAC_FILTER_NUM)
		sys_config->filter.mac_count = params->n_acl_entries;
	else
		sys_config->filter.mac_count = MAX_MAC_FILTER_NUM;

	if (params->acl_policy == NL80211_ACL_POLICY_DENY_UNLESS_LISTED)
		sys_config->filter.filter_mode = MAC_FILTER_MODE_ALLOW_MAC;
	else if (params->acl_policy == NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED)
		sys_config->filter.filter_mode = MAC_FILTER_MODE_BLOCK_MAC;
	moal_memcpy_ext(
		priv->phandle, sys_config->filter.mac_list, params->mac_addrs,
		sys_config->filter.mac_count * sizeof(mlan_802_11_mac_addr),
		sizeof(sys_config->filter.mac_list));
	if (priv->bss_started == MTRUE) {
		bss_started = MTRUE;
		if (woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT, UAP_BSS_STOP))
			PRINTM(MERROR, "%s: stop uap failed \n", __func__);
	}
	if (MLAN_STATUS_SUCCESS == woal_set_get_sys_config(priv, MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_config))
		ret = 0;
done:
	kfree(sys_config);
	if (bss_started) {
		if (woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT_TIMEOUT,
				      UAP_BSS_START))
			PRINTM(MERROR, "%s: start uap failed \n", __func__);
	}
	LEAVE();
	return ret;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
/**
 * @brief Set txq parameters

 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params		  A pointer to ieee80211_txq_params structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_txq_params(struct wiphy *wiphy, struct net_device *dev,
				 struct ieee80211_txq_params *params)
{
	int ret = 0;
	u8 ac = 0;
	wmm_parameter_t ap_wmm_para;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();

	/* AC_BE: 0, AC_BK:1, AC_VI: 2, AC_VO:3 */
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	switch (params->ac) {
	case NL80211_AC_VO:
		ac = 3;
		break;
	case NL80211_AC_VI:
		ac = 2;
		break;
	case NL80211_AC_BK:
		ac = 1;
		break;
	case NL80211_AC_BE:
		ac = 0;
		break;
	default:
		break;
	}
#else
	switch (params->queue) {
	case NL80211_TXQ_Q_VO:
		ac = 3;
		break;
	case NL80211_TXQ_Q_VI:
		ac = 2;
		break;
	case NL80211_TXQ_Q_BK:
		ac = 1;
		break;
	case NL80211_TXQ_Q_BE:
		ac = 0;
		break;
	default:
		break;
	}
#endif

	PRINTM(MMSG, "Set AC=%d, txop=%d cwmin=%d, cwmax=%d aifs=%d\n", ac,
	       params->txop, params->cwmin, params->cwmax, params->aifs);

	memset(&ap_wmm_para, 0, sizeof(wmm_parameter_t));

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_ap_wmm_para(priv, MLAN_ACT_GET, &ap_wmm_para)) {
		PRINTM(MERROR, "wlan: We don't support AP WMM parameter\n");
		LEAVE();
		return ret;
	}
	ap_wmm_para.ac_params[ac].aci_aifsn.aifsn = params->aifs;
	ap_wmm_para.ac_params[ac].ecw.ecw_max = ilog2(params->cwmax + 1);
	ap_wmm_para.ac_params[ac].ecw.ecw_min = ilog2(params->cwmin + 1);
	ap_wmm_para.ac_params[ac].tx_op_limit = params->txop;
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_ap_wmm_para(priv, MLAN_ACT_SET, &ap_wmm_para)) {
		PRINTM(MERROR, "wlan: Fail to set AP WMM parameter\n");
		ret = -EFAULT;
	}
	LEAVE();
	return ret;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
/**
 * @brief start background radar detection
 *
 * @param wiphy           A pointer to wiphy structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_set_radar_background(struct wiphy *wiphy,
				       struct cfg80211_chan_def *chandef)
{
	u32 cac_time_ms = DEF_CAC_DWELL_TIME;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv = woal_get_priv_bss_type(handle, MLAN_BSS_TYPE_DFS);
	mlan_ds_11h_chan_rep_req chan_rpt_req;
	int ret = 0;
	mlan_status status;
	ENTER();
	if (!priv) {
		PRINTM(MERROR,
		       "DFS interface not avalible in set_radar_background\n");
		return -EFAULT;
	}
	if (!chandef) {
		PRINTM(MMSG, "Stop radar background\n");
		woal_11h_cancel_chan_report_ioctl(priv, MOAL_IOCTL_WAIT);
		priv->chan_rpt_pending = MFALSE;
		priv->radar_background = MFALSE;
		woal_update_channels_dfs_state(
			priv, priv->chan_rpt_req.chanNum,
			priv->chan_rpt_req.bandcfg.chanWidth, DFS_USABLE);
		memset(&priv->chan_rpt_req, 0,
		       sizeof(mlan_ds_11h_chan_rep_req));
		LEAVE();
		return ret;
	}
	if (!(chandef->chan->flags & IEEE80211_CHAN_RADAR)) {
		PRINTM(MERROR, "Not radar channel in set_radar_background\n");
		LEAVE();
		return -EFAULT;
	}
	if ((woal_is_etsi_country(priv->phandle->country_code) == MTRUE)) {
		if (chandef->chan->hw_value == 120 ||
		    chandef->chan->hw_value == 124 ||
		    chandef->chan->hw_value == 128) {
			cac_time_ms = MAX_CAC_DWELL_TIME;
		}
		if (chandef->chan->hw_value == 116 &&
		    ((chandef->width == NL80211_CHAN_WIDTH_40) ||
		     (chandef->width == NL80211_CHAN_WIDTH_80))) {
			cac_time_ms = MAX_CAC_DWELL_TIME;
		}
	}
	if (priv->chan_rpt_req.chanNum &&
	    (priv->chan_rpt_req.chanNum != (t_u8)chandef->chan->hw_value)) {
		woal_11h_cancel_chan_report_ioctl(priv, MOAL_IOCTL_WAIT);
		priv->chan_rpt_pending = MFALSE;
		memset(&priv->chan_rpt_req, 0,
		       sizeof(mlan_ds_11h_chan_rep_req));
	}
	chan_rpt_req.startFreq = START_FREQ_11A_BAND;
	chan_rpt_req.chanNum = (t_u8)chandef->chan->hw_value;
	woal_convert_chan_to_bandconfig(priv, &chan_rpt_req.bandcfg, chandef);
	chan_rpt_req.millisec_dwell_time = cac_time_ms;
	chan_rpt_req.host_based = MTRUE;
	moal_memcpy_ext(priv->phandle, &priv->chan_rpt_req, &chan_rpt_req,
			sizeof(mlan_ds_11h_chan_rep_req),
			sizeof(mlan_ds_11h_chan_rep_req));
	PRINTM(MCMND,
	       "DFS: Start Background Radar detect on channel=%d, bandwidth=%d, cac time=%d\n",
	       chan_rpt_req.chanNum, (int)(chan_rpt_req.bandcfg.chanWidth),
	       chan_rpt_req.millisec_dwell_time);
	status = woal_do_dfs_cac(priv, &chan_rpt_req);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
	} else {
		priv->chan_rpt_pending = MTRUE;
		priv->radar_background = MTRUE;
		moal_memcpy_ext(priv->phandle, &priv->radar_background_chan,
				chandef, sizeof(struct cfg80211_chan_def),
				sizeof(struct cfg80211_chan_def));
	}
	LEAVE();
	return ret;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/**
 * @brief cac timer call back function.
 *
 * @param context   a pointer to moal_handle
 *
 * @return           N/A
 */
void woal_cac_timer_func(void *context)
{
	moal_handle *handle = (moal_handle *)context;
	moal_private *priv = handle->priv[handle->cac_bss_index];

	PRINTM(MEVENT, "cac_timer fired.\n");
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	cfg80211_cac_event(priv->netdev, &handle->dfs_channel,
			   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL, 0);
#elif CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	cfg80211_cac_event(priv->netdev, &handle->dfs_channel,
			   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
#else
	cfg80211_cac_event(priv->netdev, NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
#endif
	handle->is_cac_timer_set = MFALSE;
	memset(&handle->dfs_channel, 0, sizeof(struct cfg80211_chan_def));
	handle->cac_bss_index = 0xff;
}

/**
 * @brief This function switch AP's channel
 *        1. clear mgmt IEs  		2. stop uAP
 *   	  3. set beacon after		4. set new channel
 *   	  5. start uAP    		    6. notify cfg80211
 *
 * @param priv          a pointer to moal_private
 * @param wait_option   wait option
 *
 * @return           N/A
 */
static void woal_switch_uap_channel(moal_private *priv, t_u8 wait_option)
{
	chan_band_info uap_channel;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
	struct cfg80211_ap_update *info = container_of(
		&priv->beacon_after, struct cfg80211_ap_update, beacon);
#endif
	t_u8 chan2Offset = SEC_CHAN_NONE;
	ENTER();
	woal_clear_all_mgmt_ies(priv, MOAL_IOCTL_WAIT);
	if (woal_uap_bss_ctrl(priv, wait_option, UAP_BSS_STOP)) {
		PRINTM(MERROR, "%s: stop uap failed \n", __func__);
		goto done;
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
	if (woal_cfg80211_set_beacon(priv->wdev->wiphy, priv->netdev, info)) {
#else
	if (woal_cfg80211_set_beacon(priv->wdev->wiphy, priv->netdev,
				     &priv->beacon_after)) {
#endif
		PRINTM(MERROR, "%s: set mgmt ies failed \n", __func__);
		goto done;
	}

	uap_channel.channel = ieee80211_frequency_to_channel(
		priv->csa_chan.chan->center_freq);
	switch (priv->csa_chan.width) {
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_20_NOHT:
		uap_channel.bandcfg.chanWidth = CHAN_BW_20MHZ;
		break;
	case NL80211_CHAN_WIDTH_20:
		uap_channel.bandcfg.chanWidth = CHAN_BW_20MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		if (priv->csa_chan.center_freq1 <
		    priv->csa_chan.chan->center_freq)
			chan2Offset = SEC_CHAN_BELOW;
		else
			chan2Offset = SEC_CHAN_ABOVE;
		uap_channel.bandcfg.chanWidth = CHAN_BW_40MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		uap_channel.bandcfg.chanWidth = CHAN_BW_80MHZ;
		chan2Offset = woal_get_second_channel_offset(
			priv, uap_channel.channel);
		break;
	default:
		PRINTM(MWARN, "Unknown channel width: %d\n",
		       priv->csa_chan.width);
		break;
	}
	uap_channel.bandcfg.chanBand =
		woal_ieee_band_to_radio_type(priv->csa_chan.chan->band);
	uap_channel.bandcfg.chan2Offset = chan2Offset;
	if (MLAN_STATUS_SUCCESS != woal_set_get_ap_channel(priv, MLAN_ACT_SET,
							   wait_option,
							   &uap_channel)) {
		PRINTM(MERROR, "Fail to set ap channel \n");
		goto done;
	}
	if (woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT_TIMEOUT, UAP_BSS_START)) {
		PRINTM(MERROR, "%s: start uap failed \n", __func__);
		goto done;
	}
	PRINTM(MMSG, "CSA: old chan %d => new chan %d \n", priv->channel,
	       uap_channel.channel);
	PRINTM(MMSG, "CSA: old BW %d => new BW %d \n", priv->bandwidth,
	       uap_channel.bandcfg.chanWidth);
	priv->channel = uap_channel.channel;
	priv->bandwidth = uap_channel.bandcfg.chanWidth;
	moal_memcpy_ext(priv->phandle, &priv->chan, &priv->csa_chan,
			sizeof(struct cfg80211_chan_def), sizeof(priv->chan));
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 3, 0) &&                        \
	CFG80211_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
	cfg80211_ch_switch_notify(priv->netdev, &priv->chan, 0, 0);
#elif ((CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 1, 0) &&                    \
	(defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 33))) &&       \
	CFG80211_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
	cfg80211_ch_switch_notify(priv->netdev, &priv->chan, 0, 0);
#elif ((CFG80211_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)) ||                  \
       (defined(ANDROID_SDK_VERSION) && ANDROID_SDK_VERSION >= 31))
cfg80211_ch_switch_notify(priv->netdev, &priv->chan, 0);
#else
cfg80211_ch_switch_notify(priv->netdev, &priv->chan);
#endif
	if (priv->uap_tx_blocked) {
		if (!netif_carrier_ok(priv->netdev))
			netif_carrier_on(priv->netdev);
		woal_start_queue(priv->netdev);
		priv->uap_tx_blocked = MFALSE;
	}
done:
	LEAVE();
	return;
}

/**
 * @brief csa work handler
 *
 * @param work            a pointer to work_struct
 *
 * @return                0 -- success, otherwise fail
 */
void woal_csa_work_queue(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	moal_private *priv = container_of(delayed_work, moal_private, csa_work);
	ENTER();
	if (priv->bss_started == MTRUE)
		woal_switch_uap_channel(priv, MOAL_IOCTL_WAIT);
	LEAVE();
}

#if KERNEL_VERSION(3, 12, 0) <= CFG80211_VERSION_CODE
/*
 * @brief  handle WOAL_EVENT_CANCEL_CHANRPT
 *
 * @param priv           A pointer moal_private structure
 *
 * @return          N/A
 */
void woal_process_cancel_chanrpt_event(moal_private *priv)
{
	if (priv->phandle->is_cac_timer_set &&
	    priv->bss_index == priv->phandle->cac_bss_index) {
		woal_cancel_timer(&priv->phandle->cac_timer);
		priv->phandle->is_cac_timer_set = MFALSE;
		/* Make sure Chan Report is cancelled */
		if (woal_11h_cancel_chan_report_ioctl(priv, MOAL_IOCTL_WAIT))
			PRINTM(MERROR, "%s: cancel chan report failed \n",
			       __func__);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		cfg80211_cac_event(priv->netdev, &priv->phandle->dfs_channel,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL, 0);
#elif CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		cfg80211_cac_event(priv->netdev, &priv->phandle->dfs_channel,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
#else
		cfg80211_cac_event(priv->netdev, NL80211_RADAR_CAC_ABORTED,
				   GFP_KERNEL);
#endif
		memset(&priv->phandle->dfs_channel, 0,
		       sizeof(struct cfg80211_chan_def));
		priv->phandle->cac_bss_index = 0xff;
	}
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
/**
 * @brief start radar detection
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @param cac_time_ms     A cac dwell time
 * @param link_id	   valid link_id for MLO operation or 0 otherwise.
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_start_radar_detection(struct wiphy *wiphy,
					struct net_device *dev,
					struct cfg80211_chan_def *chandef,
					u32 cac_time_ms, int link_id)
#elif CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
/**
 * @brief start radar detection
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @param cac_time_ms     A cac dwell time
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_start_radar_detection(struct wiphy *wiphy,
					struct net_device *dev,
					struct cfg80211_chan_def *chandef,
					u32 cac_time_ms)
#else
/**
 * @brief start radar detection
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param chandef         A pointer to cfg80211_chan_def structure
 * @return                0 -- success, otherwise fail
 */

int woal_cfg80211_start_radar_detection(struct wiphy *wiphy,
					struct net_device *dev,
					struct cfg80211_chan_def *chandef)
#endif
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	moal_handle *handle = priv->phandle;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11h_chan_rep_req *pchan_rpt_req = NULL;
	mlan_ds_11h_cfg *p11h_cfg = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;
	chan_band_info channel;
	t_u16 enable = 0;
	t_u8 event_buf[64] = {0};
	ENTER();

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	PRINTM(MIOCTL, "start Radar detect, chan %d , Bw %d , Time %d \n",
	       chandef->chan->hw_value, chandef->width, cac_time_ms);
#else
	PRINTM(MIOCTL, "start Radar detect, chan %d , Bw %d \n",
	       chandef->chan->hw_value, chandef->width);
#endif

	if (priv->bss_started == MTRUE) {
		PRINTM(MERROR, "recv CAC request when bss already started \n");
		ret = -EFAULT;
		goto done;
	}
	if (priv->phandle->cac_period || handle->is_cac_timer_set) {
		PRINTM(MERROR,
		       "Maybe other interface is doing CAC, please defer your oper\n");
		ret = -EBUSY;
		goto done;
	}
	snprintf(event_buf, sizeof(event_buf) - 1, "%s %d", CUS_EVT_CAC_START,
		 chandef->chan->hw_value);
	woal_broadcast_event(priv, event_buf, strlen(event_buf));
	if (priv->phandle->card_info->drcs) {
		if (MLAN_STATUS_SUCCESS == woal_mc_policy_cfg(priv, &enable,
							      MOAL_IOCTL_WAIT,
							      MLAN_ACT_GET)) {
			if (enable) {
				if (MLAN_STATUS_SUCCESS ==
				    woal_get_active_intf_channel(priv,
								 &channel)) {
					if (channel.channel !=
					    chandef->chan->hw_value) {
						PRINTM(MERROR,
						       "DFS channel is not allowed when another connection exists on different channel\n");
						PRINTM(MERROR,
						       "Another connection's channel=%d, dfs channel=%d\n",
						       channel.channel,
						       chandef->chan->hw_value);
						ret = -EINVAL;
						goto done;
					}
				}
			}
		}
	}
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11h_cfg));
	if (NULL == req) {
		ret = -ENOMEM;
		goto done;
	}

	p11h_cfg = (mlan_ds_11h_cfg *)req->pbuf;
	pchan_rpt_req = &p11h_cfg->param.chan_rpt_req;
	pchan_rpt_req->startFreq = START_FREQ_11A_BAND;
	pchan_rpt_req->chanNum = (t_u8)chandef->chan->hw_value;
	woal_convert_chan_to_bandconfig(priv, &pchan_rpt_req->bandcfg, chandef);
	pchan_rpt_req->host_based = MTRUE;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	pchan_rpt_req->millisec_dwell_time = cac_time_ms;
#else
	pchan_rpt_req->millisec_dwell_time = IEEE80211_DFS_MIN_CAC_TIME_MS;
#endif
	/* Since kernel doesn't support 600sec cac_timer for channels 120, 124,
	 * and 128 (weather channels) in ETSI region, overwrite kernel's
	 * cac_timer.
	 */
	if ((woal_is_etsi_country(priv->phandle->country_code) == MTRUE)) {
		if (chandef->chan->hw_value == 120 ||
		    chandef->chan->hw_value == 124 ||
		    chandef->chan->hw_value == 128) {
			pchan_rpt_req->millisec_dwell_time =
				IEEE80211_DFS_MIN_CAC_TIME_MS * 10;
		}
		if (chandef->chan->hw_value == 116 &&
		    ((chandef->width == NL80211_CHAN_WIDTH_40) ||
		     (chandef->width == NL80211_CHAN_WIDTH_80))) {
			pchan_rpt_req->millisec_dwell_time =
				IEEE80211_DFS_MIN_CAC_TIME_MS * 10;
		}
	}
	if (priv->user_cac_period_msec) {
		pchan_rpt_req->millisec_dwell_time = priv->user_cac_period_msec;
		PRINTM(MCMD_D,
		       "cfg80211 dfstesting: User CAC Period=%d (msec) \n",
		       pchan_rpt_req->millisec_dwell_time);
	}

	p11h_cfg->sub_command = MLAN_OID_11H_CHANNEL_CHECK;
	req->req_id = MLAN_IOCTL_11H_CFG;
	req->action = MLAN_ACT_SET;

	/* Send Channel Check command and wait until the report is ready */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Fail to start radar detection\n");
		ret = -EFAULT;
	} else {
		moal_memcpy_ext(priv->phandle, &handle->dfs_channel, chandef,
				sizeof(struct cfg80211_chan_def),
				sizeof(handle->dfs_channel));
		handle->cac_bss_index = priv->bss_index;
		handle->is_cac_timer_set = MTRUE;
		/* avoid EVENT_CHANNEL_RAPORT_READY missing, add 1s gap */
		woal_mod_timer(&handle->cac_timer,
			       pchan_rpt_req->millisec_dwell_time + 1000);
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief channel switch

 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params		  A pointer to cfg80211_csa_settings structure
 *
 * @return                0 -- success, otherwise fail
 */
int woal_cfg80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_csa_settings *params)
{
	int ret = 0;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	t_u32 chsw_msec;
	mlan_uap_bss_param *bss_cfg = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
	struct cfg80211_ap_update *info = container_of(
		&params->beacon_csa, struct cfg80211_ap_update, beacon);
#endif

	ENTER();

	if (!params) {
		ret = -EINVAL;
		goto done;
	}
	if (params->radar_required) {
		PRINTM(MMSG, "switch to DFS channel\n");
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		woal_enable_dfs_support(priv, &params->chandef);
#endif
	}
	/* actually hostapd would always choose one diff channel*/
	if (cfg80211_chandef_identical(&params->chandef, &priv->chan)) {
		PRINTM(MMSG,
		       "csa channel is same with current channel, invaild\n");
		ret = -EINVAL;
		goto done;
	}
	bss_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!bss_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		ret = -EFAULT;
		goto done;
	}

	if (params->block_tx) {
		if (netif_carrier_ok(dev))
			netif_carrier_off(dev);
		woal_stop_queue(dev);
		priv->uap_tx_blocked = MTRUE;
	}

	woal_clear_all_mgmt_ies(priv, MOAL_IOCTL_WAIT);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
	if (woal_cfg80211_set_beacon(wiphy, dev, info)) {
#else
	if (woal_cfg80211_set_beacon(wiphy, dev, &params->beacon_csa)) {
#endif
		PRINTM(MERROR, "%s: setting csa mgmt ies failed\n", __func__);
		goto done;
	}

	moal_memcpy_ext(priv->phandle, &priv->csa_chan, &params->chandef,
			sizeof(struct cfg80211_chan_def),
			sizeof(priv->csa_chan));
	moal_memcpy_ext(priv->phandle, &priv->beacon_after,
			&params->beacon_after,
			sizeof(struct cfg80211_beacon_data),
			sizeof(priv->beacon_after));

	if (!priv->phandle->fw_ecsa_enable) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_sys_config(priv, MLAN_ACT_GET, MOAL_IOCTL_WAIT,
					    bss_cfg)) {
			PRINTM(MERROR, "%s: get uap config failed\n", __func__);
			ret = -EFAULT;
			goto done;
		}
		chsw_msec = params->count * bss_cfg->beacon_period;
		queue_delayed_work(priv->csa_workqueue, &priv->csa_work,
				   msecs_to_jiffies(chsw_msec));
	}
done:
	kfree(bss_cfg);
	LEAVE();
	return ret;
}
#endif

/**
 * @brief Register the device with cfg80211
 *
 * @param dev       A pointer to net_device structure
 * @param bss_type  BSS type
 *
 * @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status woal_register_uap_cfg80211(struct net_device *dev, t_u8 bss_type)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct wireless_dev *wdev = NULL;

	ENTER();

	wdev = (struct wireless_dev *)&priv->w_dev;
	memset(wdev, 0, sizeof(struct wireless_dev));

	wdev->wiphy = priv->phandle->wiphy;
	if (!wdev->wiphy) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (bss_type == MLAN_BSS_TYPE_UAP) {
		wdev->iftype = NL80211_IFTYPE_AP;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		woal_cfg80211_setup_uap_he_cap(priv, MOAL_IOCTL_WAIT);
#endif
	}

	dev_net_set(dev, wiphy_net(wdev->wiphy));
	dev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(dev, wiphy_dev(wdev->wiphy));
	priv->wdev = wdev;

	LEAVE();
	return ret;
}
