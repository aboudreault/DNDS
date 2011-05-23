/*
 * Generated by asn1c-0.9.22 (http://lionet.info/asn1c)
 * From ASN.1 module "DNDS"
 * 	found in "dnds.asn1"
 */

#ifndef	_Peer_H_
#define	_Peer_H_


#include <asn_application.h>

/* Including external dependencies */
#include <NativeInteger.h>
#include <PrintableString.h>
#include <OCTET_STRING.h>
#include <BIT_STRING.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Peer */
typedef struct Peer {
	unsigned long	 id;
	unsigned long	 contextId;
	PrintableString_t	*name	/* OPTIONAL */;
	OCTET_STRING_t	*ipAddress	/* OPTIONAL */;
	PrintableString_t	*certificate	/* OPTIONAL */;
	BIT_STRING_t	*certificateKey	/* OPTIONAL */;
	long	*status	/* OPTIONAL */;
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	
	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} Peer_t;

/* Implementation */
/* extern asn_TYPE_descriptor_t asn_DEF_id_2;	// (Use -fall-defs-global to expose) */
/* extern asn_TYPE_descriptor_t asn_DEF_contextId_3;	// (Use -fall-defs-global to expose) */
extern asn_TYPE_descriptor_t asn_DEF_Peer;

#ifdef __cplusplus
}
#endif

#endif	/* _Peer_H_ */
#include <asn_internal.h>
