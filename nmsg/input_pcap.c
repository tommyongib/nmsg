/*
 * Copyright (c) 2009 by Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Import. */

#include <stdio.h>

#include "asprintf.h"

/* Export. */

nmsg_pcap
nmsg_input_open_pcap(pcap_t *phandle) {
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

	return (pcap);
}

nmsg_res
nmsg_input_close_pcap(nmsg_pcap *pcap) {
	pcap_freecode(&(*pcap)->userbpf);
	pcap_close((*pcap)->handle);
	reasm_ip_free((*pcap)->reasm);
	free((*pcap)->new_pkt);
	free((*pcap)->userbpft);
	free(*pcap);
	*pcap = NULL;
	return (nmsg_res_success);
}

nmsg_res
nmsg_input_next_pcap(nmsg_pcap pcap, struct nmsg_ipdg *dg) {
	const u_char *pkt_data;
	int pcap_res;
	struct pcap_pkthdr *pkt_hdr;

	/* get the next frame from the libpcap source */
	pcap_res = pcap_next_ex(pcap->handle, &pkt_hdr, &pkt_data);
	if (pcap_res == -1)
		return (nmsg_res_pcap_error);
	if (pcap_res == -2)
		return (nmsg_res_eof);

	/* parse the frame */
	return (nmsg_ipdg_parse_pcap(dg, pcap, pkt_hdr, pkt_data));
}

nmsg_res
nmsg_pcap_setfilter(nmsg_pcap pcap, const char *userbpft) {
	static const char *bpf_ipv4_frags = "(ip[6:2] & 0x1fff != 0)";
	static const char *bpf_ip = "(ip)";
	static const char *bpf_ip6 = "(ip6)";

	bool need_ip6 = true;
	bool need_ipv4_frags = true;
	bool userbpf_ip_only = true;
	char *tmp, *bpfstr;
	int res;
	struct bpf_program bpf;

	/* free an old filter set by a previous call */
	free(pcap->userbpft);
	pcap_freecode(&pcap->userbpf);

	/* compile the user's bpf and save it */
	res = pcap_compile(pcap->handle, &pcap->userbpf, userbpft, 1, 0);
	if (res != 0) {
		fprintf(stderr, "%s: unable to compile bpf '%s': %s\n",
			__func__, userbpft, pcap_geterr(pcap->handle));
		return (nmsg_res_failure);
	}
	pcap->userbpft = strdup(userbpft);

	/* test if we can skip ip6 */
	res = nmsg_asprintf(&tmp, "(%s) and %s", userbpft, bpf_ip6);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	if (res != 0)
		need_ip6 = false;
	else
		pcap_freecode(&bpf);
	fprintf(stderr, "%s: need_ip6=%d tmp='%s'\n", __func__, need_ip6, tmp);
	free(tmp);

	/* test if we can skip ipv4 frags */
	res = nmsg_asprintf(&tmp, "(%s) and %s", userbpft, bpf_ipv4_frags);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	if (res != 0)
		need_ipv4_frags = false;
	else
		pcap_freecode(&bpf);
	fprintf(stderr, "%s: need_ipv4_frags=%d tmp='%s'\n", __func__, need_ipv4_frags, tmp);
	free(tmp);

	/* test if we can limit the userbpf to ip packets only */
	res = nmsg_asprintf(&tmp, "%s and (%s)", bpf_ip, userbpft);
	if (res == -1)
		return (nmsg_res_memfail);
	res = pcap_compile(pcap->handle, &bpf, tmp, 1, 0);
	if (res != 0)
		userbpf_ip_only = false;
	else
		pcap_freecode(&bpf);
	fprintf(stderr, "%s: userbpf_ip_only=%d tmp='%s'\n", __func__, userbpf_ip_only, tmp);
	free(tmp);

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

	res = nmsg_asprintf(&bpfstr, "%s or (vlan and %s)", tmp, tmp);
	if (res == -1) {
		free(tmp);
		return (nmsg_res_memfail);
	}

	fprintf(stderr, "%s: final bpf expression: '%s'\n", __func__, bpfstr);
	res = pcap_compile(pcap->handle, &bpf, bpfstr, 1, 0);
	if (res != 0) {
		free(tmp);
		free(bpfstr);
		return (nmsg_res_failure);
	}

	/* load the constructed bpf */
	if (pcap_setfilter(pcap->handle, &bpf) != 0) {
		fprintf(stderr, "%s: unable to set filter: %s\n",
			__func__, pcap_geterr(pcap->handle));
		return (nmsg_res_failure);
	}

	/* cleanup */
	free(tmp);
	free(bpfstr);
	pcap_freecode(&bpf);

	return (nmsg_res_success);
}
