#ifndef PROTOBUF_C_ncap_2eproto__INCLUDED
#define PROTOBUF_C_ncap_2eproto__INCLUDED

#include <nmsg/protobuf-c.h>

PROTOBUF_C_BEGIN_DECLS


typedef struct _Nmsg__Isc__Ncap Nmsg__Isc__Ncap;


/* --- enums --- */


/* --- messages --- */

struct  _Nmsg__Isc__Ncap
{
  ProtobufCMessage base;
  uint32_t ether_type;
  ProtobufCBinaryData payload;
};
#define NMSG__ISC__NCAP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nmsg__isc__ncap__descriptor) \
    , 0, {0,NULL} }


/* Nmsg__Isc__Ncap methods */
void   nmsg__isc__ncap__init
                     (Nmsg__Isc__Ncap         *message);
size_t nmsg__isc__ncap__get_packed_size
                     (const Nmsg__Isc__Ncap   *message);
size_t nmsg__isc__ncap__pack
                     (const Nmsg__Isc__Ncap   *message,
                      uint8_t             *out);
size_t nmsg__isc__ncap__pack_to_buffer
                     (const Nmsg__Isc__Ncap   *message,
                      ProtobufCBuffer     *buffer);
Nmsg__Isc__Ncap *
       nmsg__isc__ncap__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nmsg__isc__ncap__free_unpacked
                     (Nmsg__Isc__Ncap *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Nmsg__Isc__Ncap_Closure)
                 (const Nmsg__Isc__Ncap *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor nmsg__isc__ncap__descriptor;

PROTOBUF_C_END_DECLS


#endif  /* PROTOBUF_ncap_2eproto__INCLUDED */
