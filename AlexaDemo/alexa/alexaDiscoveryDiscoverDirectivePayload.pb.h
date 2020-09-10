/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.0 */

#ifndef PB_ALEXADISCOVERY_ALEXADISCOVERYDISCOVERDIRECTIVEPAYLOAD_PB_H_INCLUDED
#define PB_ALEXADISCOVERY_ALEXADISCOVERYDISCOVERDIRECTIVEPAYLOAD_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Struct definitions */
typedef struct _alexaDiscovery_DiscoverDirectivePayloadProto_Scope {
    char type[32];
    char token[32];
} alexaDiscovery_DiscoverDirectivePayloadProto_Scope;

typedef struct _alexaDiscovery_DiscoverDirectivePayloadProto {
    bool has_scope;
    alexaDiscovery_DiscoverDirectivePayloadProto_Scope scope;
} alexaDiscovery_DiscoverDirectivePayloadProto;


/* Initializer values for message structs */
#define alexaDiscovery_DiscoverDirectivePayloadProto_init_default {false, alexaDiscovery_DiscoverDirectivePayloadProto_Scope_init_default}
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_init_default {"", ""}
#define alexaDiscovery_DiscoverDirectivePayloadProto_init_zero {false, alexaDiscovery_DiscoverDirectivePayloadProto_Scope_init_zero}
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_init_zero {"", ""}

/* Field tags (for use in manual encoding/decoding) */
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_type_tag 1
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_token_tag 2
#define alexaDiscovery_DiscoverDirectivePayloadProto_scope_tag 1

/* Struct field encoding specification for nanopb */
#define alexaDiscovery_DiscoverDirectivePayloadProto_FIELDLIST(X, a) \
X(a, STATIC,   OPTIONAL, MESSAGE,  scope,             1)
#define alexaDiscovery_DiscoverDirectivePayloadProto_CALLBACK NULL
#define alexaDiscovery_DiscoverDirectivePayloadProto_DEFAULT NULL
#define alexaDiscovery_DiscoverDirectivePayloadProto_scope_MSGTYPE alexaDiscovery_DiscoverDirectivePayloadProto_Scope

#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, STRING,   type,              1) \
X(a, STATIC,   SINGULAR, STRING,   token,             2)
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_CALLBACK NULL
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_DEFAULT NULL

extern const pb_msgdesc_t alexaDiscovery_DiscoverDirectivePayloadProto_msg;
extern const pb_msgdesc_t alexaDiscovery_DiscoverDirectivePayloadProto_Scope_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define alexaDiscovery_DiscoverDirectivePayloadProto_fields &alexaDiscovery_DiscoverDirectivePayloadProto_msg
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_fields &alexaDiscovery_DiscoverDirectivePayloadProto_Scope_msg

/* Maximum encoded size of messages (where known) */
#define alexaDiscovery_DiscoverDirectivePayloadProto_size 68
#define alexaDiscovery_DiscoverDirectivePayloadProto_Scope_size 66

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif