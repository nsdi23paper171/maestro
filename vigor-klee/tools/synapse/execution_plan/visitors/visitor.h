#pragma once

#include <memory>

namespace synapse {

class ExecutionPlan;
class ExecutionPlanNode;

namespace targets {
namespace x86 {
class MapGet;
class CurrentTime;
class PacketBorrowNextChunk;
class PacketGetMetadata;
class PacketReturnChunk;
class If;
class Then;
class Else;
class Forward;
class Broadcast;
class Drop;
class ExpireItemsSingleMap;
class RteEtherAddrHash;
class DchainRejuvenateIndex;
class VectorBorrow;
class VectorReturn;
class DchainAllocateNewIndex;
class MapPut;
class PacketGetUnreadLength;
class SetIpv4UdpTcpChecksum;
class DchainIsIndexAllocated;
} // namespace x86

namespace tofino {} // namespace tofino

namespace BMv2SimpleSwitchgRPC {
class SendToController;
class ParserConsume;
class Ignore;
class SetupExpirationNotifications;
class If;
class Then;
class Else;
class EthernetConsume;
class EthernetModify;
class TableLookup;
class IPv4Consume;
class IPv4Modify;
class TcpUdpConsume;
class TcpUdpModify;
class IPOptionsConsume;
class IPOptionsModify;
class Drop;
class Forward;
class VectorReturn;
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets

class ExecutionPlanVisitor {
public:
  virtual void visit(ExecutionPlan ep);
  virtual void visit(const ExecutionPlanNode *ep_node);

  virtual void visit(const targets::x86::MapGet *node) {}
  virtual void visit(const targets::x86::CurrentTime *node) {}
  virtual void visit(const targets::x86::PacketBorrowNextChunk *node) {}
  virtual void visit(const targets::x86::PacketReturnChunk *node) {}
  virtual void visit(const targets::x86::PacketGetMetadata *node) {}
  virtual void visit(const targets::x86::If *node) {}
  virtual void visit(const targets::x86::Then *node) {}
  virtual void visit(const targets::x86::Else *node) {}
  virtual void visit(const targets::x86::Forward *node) {}
  virtual void visit(const targets::x86::Broadcast *node) {}
  virtual void visit(const targets::x86::Drop *node) {}
  virtual void visit(const targets::x86::ExpireItemsSingleMap *node) {}
  virtual void visit(const targets::x86::RteEtherAddrHash *node) {}
  virtual void visit(const targets::x86::DchainRejuvenateIndex *node) {}
  virtual void visit(const targets::x86::VectorBorrow *node) {}
  virtual void visit(const targets::x86::VectorReturn *node) {}
  virtual void visit(const targets::x86::DchainAllocateNewIndex *node) {}
  virtual void visit(const targets::x86::MapPut *node) {}
  virtual void visit(const targets::x86::PacketGetUnreadLength *node) {}
  virtual void visit(const targets::x86::SetIpv4UdpTcpChecksum *node) {}
  virtual void visit(const targets::x86::DchainIsIndexAllocated *node) {}

  virtual void
  visit(const targets::BMv2SimpleSwitchgRPC::SendToController *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::Ignore *node) {}
  virtual void visit(
      const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node) {
  }
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::If *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::Then *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::Else *node) {}
  virtual void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetConsume *node) {}
  virtual void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::TcpUdpConsume *node) {
  }
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::TcpUdpModify *node) {}
  virtual void
  visit(const targets::BMv2SimpleSwitchgRPC::IPOptionsConsume *node) {}
  virtual void
  visit(const targets::BMv2SimpleSwitchgRPC::IPOptionsModify *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::Drop *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::Forward *node) {}
  virtual void visit(const targets::BMv2SimpleSwitchgRPC::VectorReturn *node) {}
};

} // namespace synapse
