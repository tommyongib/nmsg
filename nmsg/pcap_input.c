/*
 * Copyright (c) 2009, 2011 by Farsight Security, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Import. */

#include <pcap.h>

#include "private.h"

/* Export. */

nmsg_pcap_t
nmsg_pcap_input_open(pcap_t *phandle) {
	struct nmsg_pcap *pcap;

	pcap = calloc(1, sizeof(*pcap));
	if (pcap == NULL)
		return (NULL);

	pcap->handle = phandle;
	pcap->datalink = pcap_datalink(phandle);
	pcap->new_pkt = calloc(1, NMSG_IPSZ_MAX);
	pcap->reasm = reasm_ip_new();
	if (pcap->reasm == NULL) {
		free(pcap->new_pkt);
		free(pcap);
		return (NULL);
	}
	reasm_ip_set_timeout(pcap->reasm, 60);

	if (pcap_file(phandle) == NULL)
		pcap->type = nmsg_pcap_type_live;
	else
		pcap->type = nmsg_pcap_type_file;

	return (pcap);
}

nmsg_res
nmsg_pcap_input_close(nmsg_pcap_t *pcap) {
	pcap_freecode(&(*pcap)->userbpf);
	pcap_close((*pcap)->handle);
	if ((*pcap)->user != NULL)
		pcap_close((*pcap)->user);

	reasm_ip_free((*pcap)->reasm);

	free((*pcap)->new_pkt);
	free((*pcap)->userbpft);
	free(*pcap);

	*pcap = NULL;
	return (nmsg_res_success);
}

nmsg_res
nmsg_pcap_input_read(nmsg_pcap_t pcap, struct nmsg_ipdg *dg,
		     struct timespec *ts)
{
	const u_char *pkt_data;
	int pcap_res;
	struct pcap_pkthdr *pkt_hdr;

	assert(pcap->raw == false);

	/* get the next frame from the libpcap source */
	pcap_res = pcap_next_ex(pcap->handle, &pkt_hdr, &pkt_data);
	if (pcap_res == 0)
		return (nmsg_res_again);
	if (pcap_res == -1) {
		_nmsg_dprintf(1, "%s: pcap_next_ex() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		return (nmsg_res_pcap_error);
	}
	if (pcap_res == -2)
		return (nmsg_res_eof);

	/* get the time of packet reception */
	ts->tv_sec = pkt_hdr->ts.tv_sec;
	ts->tv_nsec = pkt_hdr->ts.tv_usec * 1000;

	/* parse the frame */
	return (nmsg_ipdg_parse_pcap(dg, pcap, pkt_hdr, pkt_data));
}

nmsg_res
nmsg_pcap_input_read_raw(nmsg_pcap_t pcap, struct pcap_pkthdr **pkt_hdr,
			 const uint8_t **pkt_data, struct timespec *ts)
{
	int pcap_res;

	assert(pcap->raw == true);

	/* get the next frame from the libpcap source */
	pcap_res = pcap_next_ex(pcap->handle, pkt_hdr, (const u_char **) pkt_data);
	if (pcap_res == 0)
		return (nmsg_res_again);
	if (pcap_res == -1) {
		_nmsg_dprintf(1, "%s: pcap_next_ex() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		return (nmsg_res_pcap_error);
	}
	if (pcap_res == -2)
		return (nmsg_res_eof);

	/* get the time of packet reception */
	ts->tv_sec = (*pkt_hdr)->ts.tv_sec;
	ts->tv_nsec = (*pkt_hdr)->ts.tv_usec * 1000;

	return (nmsg_res_success);
}

void
nmsg_pcap_input_set_raw(nmsg_pcap_t pcap, bool raw) {
	pcap->raw = raw;
}

nmsg_res
nmsg_pcap_input_setfilter_raw(nmsg_pcap_t pcap, const char *userbpft) {
	bool need_vlan = false;
	char *tmp, *bpfstr;
	int res;
	struct bpf_program bpf;

	tmp = strdup(userbpft);
	assert(tmp != NULL);

	/* open a dummy pcap_t for the user bpf */
	if (pcap->user == NULL) {
		pcap->user = pcap_open_dead(DLT_RAW, 1500);
		if (pcap->user == NULL) {
			free(tmp);
			return (nmsg_res_memfail);
		}
	}

	/* free an old filter set by a previous call */
	free(pcap->userbpft);
	pcap_freecode(&pcap->userbpf);

	/* compile the user's bpf and save it */
	res = pcap_compile(pcap->user, &pcap->userbpf, (char *) userbpft, 1, 0);
	if (res != 0) {
		_nmsg_dprintf(1, "%s: unable to compile bpf '%s': %s\n", __func__,
			      userbpft, pcap_geterr(pcap->handle));
		free(tmp);
		return (nmsg_res_failure);
	}
	pcap->userbpft = strdup(userbpft);

	/* test if we can skip vlan tags */
	res = pcap_compile(pcap->handle, &bpf, "vlan and ip", 1, 0);
	if (res == 0) {
		pcap_freecode(&bpf);
		need_vlan = true;
	}
	_nmsg_dprintf(5, "%s: need_vlan=%u\n", __func__, need_vlan);

	/* construct and compile the final bpf */
	if (need_vlan) {
		res = nmsg_asprintf(&bpfstr, "(%s) or (vlan and (%s))", tmp, tmp);
		if (res == -1) {
			free(tmp);
			return (nmsg_res_memfail);
		}
	} else {
		bpfstr = tmp;
		tmp = NULL;
	}

	_nmsg_dprintf(3, "%s: using bpf '%s'\n", __func__, bpfstr);
	res = pcap_compile(pcap->handle, &bpf, bpfstr, 1, 0);
	if (res != 0) {
		_nmsg_dprintf(1, "%s: pcap_compile() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		free(tmp);
		free(bpfstr);
		return (nmsg_res_failure);
	}

	/* load the constructed bpf */
	if (pcap_setfilter(pcap->handle, &bpf) != 0) {
		_nmsg_dprintf(1, "%s: pcap_setfilter() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		free(tmp);
		free(bpfstr);
		return (nmsg_res_failure);
	}

	/* cleanup */
	free(tmp);
	free(bpfstr);
	pcap_freecode(&bpf);

	return (nmsg_res_success);
}

nmsg_res
nmsg_pcap_input_setfilter(nmsg_pcap_t pcap, const char *userbpft) {
	static const char *bpf_ipv4_frags = "(ip[6:2] & 0x3fff != 0)";
	static const char *bpf_ip = "(ip)";
	static const char *bpf_ip6 = "(ip6)";

	bool need_ip6 = true;
	bool need_ipv4_frags = true;
	bool need_vlan = false;
	bool userbpf_ip_only = true;
	char *tmp, *bpfstr;
	int res;
	struct bpf_program bpf;

	/* open a dummy pcap_t for the user bpf */
	if (pcap->user == NULL) {
		pcap->user = pcap_open_dead(DLT_RAW, 1500);
		if (pcap->user == NULL)
			return (nmsg_res_memfail);
	}

	/* free an old filter set by a previous call */
	free(pcap->userbpft);
	pcap_freecode(&pcap->userbpf);

	/* compile the user's bpf and save it */
	res = pcap_compile(pcap->user, &pcap->userbpf, (char *) userbpft, 1, 0);
	if (res != 0) {
		_nmsg_dprintf(1, "%s: unable to compile bpf '%s': %s\n", __func__,
			      userbpft, pcap_geterr(pcap->handle));
		return (nmsg_res_failure);
	}
	pcap->userbpft = strdup(userbpft);

	/* test if we can skip ip6 */
	res = nmsg_asprintf(&tmp, "(%s) and %s", userbpft, bpf_ip6);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	free(tmp);
	if (res != 0)
		need_ip6 = false;
	else
		pcap_freecode(&bpf);

	_nmsg_dprintf(5, "%s: need_ip6=%u\n", __func__, need_ip6);

	/* test if we can skip ipv4 frags */
	res = nmsg_asprintf(&tmp, "(%s) and %s", userbpft, bpf_ipv4_frags);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	free(tmp);
	if (res != 0)
		need_ipv4_frags = false;
	else
		pcap_freecode(&bpf);

	_nmsg_dprintf(5, "%s: need_ipv4_frags=%u\n", __func__, need_ipv4_frags);

	/* test if we can skip vlan tags */
	res = pcap_compile(pcap->handle, &bpf, "vlan and ip", 1, 0);
	if (res == 0) {
		pcap_freecode(&bpf);
		need_vlan = true;
	}
	_nmsg_dprintf(5, "%s: need_vlan=%u\n", __func__, need_vlan);

	/* test if we can limit the userbpf to ip packets only */
	res = nmsg_asprintf(&tmp, "%s and (%s)", bpf_ip, userbpft);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	free(tmp);
	if (res != 0)
		userbpf_ip_only = false;
	else
		pcap_freecode(&bpf);

	_nmsg_dprintf(5, "%s: userbpf_ip_only=%u\n", __func__, userbpf_ip_only);

	/* construct and compile the final bpf */
	res = nmsg_asprintf(&tmp, "((%s%s(%s))%s%s%s%s)",
			    userbpf_ip_only ?	bpf_ip		: "",
			    userbpf_ip_only ?	" and "		: "",
			    userbpft,
			    need_ipv4_frags ?	" or "		: "",
			    need_ipv4_frags ?	bpf_ipv4_frags	: "",
			    need_ip6 ?		" or "		: "",
			    need_ip6 ?		bpf_ip6		: "");
	if (res == -1)
		return (nmsg_res_memfail);

	if (need_vlan) {
		res = nmsg_asprintf(&bpfstr, "%s or (vlan and %s)", tmp, tmp);
		if (res == -1) {
			free(tmp);
			return (nmsg_res_memfail);
		}
	} else {
		bpfstr = tmp;
		tmp = NULL;
	}

	_nmsg_dprintf(3, "%s: using bpf '%s'\n", __func__, bpfstr);
	res = pcap_compile(pcap->handle, &bpf, bpfstr, 1, 0);
	if (res != 0) {
		_nmsg_dprintf(1, "%s: pcap_compile() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		free(tmp);
		free(bpfstr);
		return (nmsg_res_failure);
	}

	/* load the constructed bpf */
	if (pcap_setfilter(pcap->handle, &bpf) != 0) {
		_nmsg_dprintf(1, "%s: pcap_setfilter() failed: %s\n", __func__,
			      pcap_geterr(pcap->handle));
		return (nmsg_res_failure);
	}

	/* cleanup */
	free(tmp);
	free(bpfstr);
	pcap_freecode(&bpf);

	return (nmsg_res_success);
}

int
nmsg_pcap_snapshot(nmsg_pcap_t pcap) {
	return (pcap_snapshot(pcap->handle));
}

nmsg_pcap_type
nmsg_pcap_get_type(nmsg_pcap_t pcap) {
	return (pcap->type);
}

int
nmsg_pcap_get_datalink(nmsg_pcap_t pcap) {
	return (pcap->datalink);
}

bool
nmsg_pcap_filter(nmsg_pcap_t pcap, const uint8_t *pkt, size_t len) {
	struct bpf_insn *fcode;

	fcode = pcap->userbpf.bf_insns;

	if (fcode != NULL) {
		return (bpf_filter(fcode, (u_char *) pkt, len, len) != 0);
	} else {
		return (true);
	}
}
