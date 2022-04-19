#pragma once

#include "../module.h"

#include "drop.h"
#include "else.h"
#include "ethernet_consume.h"
#include "ethernet_modify.h"
#include "forward.h"
#include "if.h"
#include "ignore.h"
#include "ip_options_consume.h"
#include "ip_options_modify.h"
#include "ipv4_consume.h"
#include "ipv4_modify.h"
#include "send_to_controller.h"
#include "setup_expiration_notifications.h"
#include "table_lookup.h"
#include "tcpudp_consume.h"
#include "tcpudp_modify.h"
#include "then.h"
#include "vector_return.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
    MODULE(SendToController), MODULE(Ignore),
    MODULE(If),               MODULE(Then),
    MODULE(Else),             MODULE(SetupExpirationNotifications),
    MODULE(EthernetConsume),  MODULE(TableLookup),
    MODULE(IPv4Consume),      MODULE(TcpUdpConsume),
    MODULE(EthernetModify),   MODULE(IPv4Modify),
    MODULE(Drop),             MODULE(Forward),
    MODULE(VectorReturn),     MODULE(IPOptionsConsume),
    MODULE(TcpUdpModify),     MODULE(IPOptionsModify),
  };

  return modules;
}
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
