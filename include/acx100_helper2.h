/* include/acx100_helper2.h
 *
 * --------------------------------------------------------------------
 *
 * Copyright (C) 2003  ACX100 Open Source Project
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the ACX100 Open Source Project can be
 * made directly to:
 *
 * acx100-users@lists.sf.net
 * http://acx100.sf.net
 *
 * --------------------------------------------------------------------
 */
#ifndef __ACX_ACX100_HELPER2_H
#define __ACX_ACX100_HELPER2_H

typedef struct acx_frame {
	char val0x0[0x26];
	UINT16 val0x26;
	acx_addr3_t fr;
} acx_frame_t;

typedef struct alloc_p80211mgmt_req_t {
	union {
		wlan_fr_mgmt_t mgmt;
		wlan_fr_assocreq_t assocreq;
		wlan_fr_reassocreq_t reassocreq;
		wlan_fr_assocresp_t assocresp;
		wlan_fr_reassocresp_t reassocresp;
		wlan_fr_beacon_t beacon;
		wlan_fr_disassoc_t disassoc;
		wlan_fr_authen_t authen;
		wlan_fr_deauthen_t deauthen;
		wlan_fr_proberesp_t proberesp;
	} a;
} alloc_p80211_mgmt_req_t;

typedef struct rxhostdescriptor rxhostdescriptor_t;
typedef struct rxbuffer rxbuffer_t;

#define MAX_NUMBER_OF_SITE 31

typedef struct __WLAN_ATTRIB_PACK__ ssid {
	UINT8 element_ID;
	UINT8 length;
	UINT8 inf[32];
} ssid_t;

typedef struct __WLAN_ATTRIB_PACK__ rates {
	UINT8 element_ID;
	UINT8 length;
	UINT8 sup_rates[8];
} rates_t;

typedef struct __WLAN_ATTRIB_PACK__ fhps {
	UINT8 element_ID;
	UINT8 length;
	UINT16 dwell_time;
	UINT8 hop_set;
	UINT8 hop_pattern;
	UINT8 hop_index;
} fhps_t;

typedef struct __WLAN_ATTRIB_PACK__ dsps {
	UINT8 element_ID;
	UINT8 length;
	UINT8 curr_channel;
} dsps_t;

typedef struct __WLAN_ATTRIB_PACK__ cfps {
	UINT8 element_ID;
	UINT8 length;
	UINT8 cfp_count;
	UINT8 cfp_period;
	UINT16 cfp_max_dur;
	UINT16 cfp_dur_rem;
} cfps_t;

typedef struct __WLAN_ATTRIB_PACK__ challenge_text {
	UINT8 element_ID;
	UINT8 length;
	UINT8 text[253];
} challenge_text_t;



typedef struct __WLAN_ATTRIB_PACK__ auth_frame_body {
	UINT16 auth_alg;
	UINT16 auth_seq;
	UINT16 status;
	challenge_text_t challenge;
} auth_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ assocresp_frame_body {
	UINT16 cap_info;
	UINT16 status;
	UINT16 aid;
	rates_t rates;
} assocresp_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ reassocreq_frame_body {
	UINT16 cap_info;
	UINT16 listen_int;
	UINT8 current_ap[6];
	ssid_t ssid;
	rates_t rates;
} reassocreq_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ reassocresp_frame_body {
	UINT16 cap_info;
	UINT16 status;
	UINT16 aid;
	rates_t rates;
} reassocresp_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ deauthen_frame_body {
	UINT16 reason;
} deauthen_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ disassoc_frame_body {
	UINT16 reason;
} disassoc_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ probereq_frame_body {
	ssid_t ssid;
	rates_t rates;
} probereq_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ proberesp_frame_body {
	UINT8 timestamp[8];
	UINT16 beacon_int;
	UINT16 cap_info;
	ssid_t ssid;
	rates_t rates;
	fhps_t fhps;
	dsps_t dsps;
	cfps_t cfps;
} proberesp_frame_body_t;

typedef struct __WLAN_ATTRIB_PACK__ TxData {
	UINT16 frame_control;	/* 0x0 */
	UINT16 duration_id;	/* 0x2 */
	UINT8 da[ETH_ALEN];	/* 0x4 */
	UINT8 sa[ETH_ALEN];	/* 0xa */
	UINT8 bssid[ETH_ALEN];	/* 0x10 */
	UINT16 sequence_control;	/* 0x16 */
	union {
		auth_frame_body_t auth;
		deauthen_frame_body_t deauthen;
		/* assocreq_frame_body_t does not exist, since it
		 * contains variable-length members, thus it's no static
		 * struct */
		assocresp_frame_body_t assocresp;
		reassocresp_frame_body_t reassocreq;
		reassocresp_frame_body_t reassocresp;
		disassoc_frame_body_t disassoc;
		probereq_frame_body_t probereq;
		proberesp_frame_body_t proberesp;
		char * raw[2400-24];
	} body;
} TxData;			/* size: 2400 */

void acx_sta_list_init(wlandevice_t *priv);
client_t *acx_sta_list_alloc(wlandevice_t *priv, const UINT8 *address);
client_t *acx_sta_list_add(wlandevice_t *priv, const UINT8 *address);
client_t *acx_sta_list_get(wlandevice_t *priv, const UINT8 *address);
const char *acx_get_status_name(UINT16 status);
void acx_set_status(wlandevice_t *priv, UINT16 status);
int acx_rx_ieee802_11_frame(wlandevice_t *priv, rxhostdescriptor_t *desc);
UINT32 acx_transmit_assocresp(wlan_fr_assocreq_t *arg_0,
			  wlandevice_t *priv);
UINT32 acx_transmit_reassocresp(wlan_fr_reassocreq_t *arg_0,
			    wlandevice_t *priv);
int acx_process_disassoc(wlan_fr_disassoc_t *req, wlandevice_t *priv);
int acx_process_disassociate(wlan_fr_disassoc_t *req, wlandevice_t *priv);
void acx_process_probe_response(struct rxbuffer *mmt, wlandevice_t *priv,
			  acxp80211_hdr_t *hdr);
int acx_process_assocresp(wlan_fr_assocresp_t *req, wlandevice_t *priv);
int acx_process_reassocresp(wlan_fr_reassocresp_t *req, wlandevice_t *priv);
int acx_process_authen(wlan_fr_authen_t *req, wlandevice_t *priv);

int acx_process_deauthen(wlan_fr_deauthen_t *req, wlandevice_t *priv);
int acx_process_deauthenticate(wlan_fr_deauthen_t *req, wlandevice_t *priv);
int acx_transmit_deauthen(const UINT8 *addr, client_t *arg_4, wlandevice_t *priv,
		      UINT16 reason);
int acx_transmit_authen1(wlandevice_t *priv);
int acx_transmit_authen2(wlan_fr_authen_t *arg_0, client_t *sta_list,
		      wlandevice_t *priv);
int acx_transmit_authen3(wlan_fr_authen_t *arg_0, wlandevice_t *priv);
int acx_transmit_authen4(wlan_fr_authen_t *arg_0, wlandevice_t *priv);
int acx_transmit_assoc_req(wlandevice_t *priv);
UINT32 acx_transmit_disassoc(client_t *arg_0, wlandevice_t *priv);
#if (POWER_SAVE_80211 == 0)
void ActivatePowerSaveMode(wlandevice_t *priv, int vala);
#endif
void acx_timer(unsigned long a);
void acx_complete_dot11_scan(wlandevice_t *priv);
#endif /* __ACX_ACX100_HELPER2_H */
