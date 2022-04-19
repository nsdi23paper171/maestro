#pragma once

#include "../module.h"

#include "current_time.h"
#include "nf_set_rte_ipv4_udptcp_checksum.h"
#include "rte_ether_addr_hash.h"

#include "packet_borrow_next_chunk.h"
#include "packet_get_metadata.h"
#include "packet_get_unread_length.h"
#include "packet_return_chunk.h"

#include "else.h"
#include "if.h"
#include "then.h"

#include "broadcast.h"
#include "drop.h"
#include "forward.h"

#include "dchain_allocate_new_index.h"
#include "dchain_is_index_allocated.h"
#include "dchain_rejuvenate_index.h"

#include "vector_borrow.h"
#include "vector_return.h"

#include "expire_items_single_map.h"
#include "map_get.h"
#include "map_put.h"

namespace synapse {
namespace targets {
namespace x86 {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
    MODULE(MapGet),                 MODULE(CurrentTime),
    MODULE(PacketBorrowNextChunk),  MODULE(PacketReturnChunk),
    MODULE(PacketGetMetadata),      MODULE(If),
    MODULE(Then),                   MODULE(Else),
    MODULE(Forward),                MODULE(Broadcast),
    MODULE(Drop),                   MODULE(ExpireItemsSingleMap),
    MODULE(RteEtherAddrHash),       MODULE(DchainRejuvenateIndex),
    MODULE(VectorBorrow),           MODULE(VectorReturn),
    MODULE(DchainAllocateNewIndex), MODULE(MapPut),
    MODULE(PacketGetUnreadLength),  MODULE(SetIpv4UdpTcpChecksum),
    MODULE(DchainIsIndexAllocated),
  };

  return modules;
}
} // namespace x86
} // namespace targets
} // namespace synapse
