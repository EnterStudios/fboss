/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/RouteTable.h"
#include "fboss/agent/state/RouteTableMap.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"

#include "fboss/agent/state/NodeBase-defs.h"

using std::make_shared;
using std::shared_ptr;
using std::chrono::seconds;

namespace {
constexpr auto kInterfaces = "interfaces";
constexpr auto kPorts = "ports";
constexpr auto kVlans = "vlans";
constexpr auto kRouteTables = "routeTables";
constexpr auto kDefaultVlan = "defaultVlan";
constexpr auto kAcls = "acls";
constexpr auto kArpTimeout = "arpTimeout";
constexpr auto kNdpTimeout = "ndpTimeout";
constexpr auto kArpAgerInterval = "arpAgerInterval";
constexpr auto kMaxNeighborProbes = "maxNeighborProbes";
constexpr auto kStaleEntryInterval = "staleEntryInterval";
}

namespace facebook { namespace fboss {

SwitchStateFields::SwitchStateFields()
    : ports(make_shared<PortMap>()),
      aggPorts(make_shared<AggregatePortMap>()),
      vlans(make_shared<VlanMap>()),
      interfaces(make_shared<InterfaceMap>()),
      routeTables(make_shared<RouteTableMap>()),
      acls(make_shared<AclMap>()) {}

folly::dynamic SwitchStateFields::toFollyDynamic() const {
  folly::dynamic switchState = folly::dynamic::object;
  switchState[kInterfaces] = interfaces->toFollyDynamic();
  switchState[kPorts] = ports->toFollyDynamic();
  switchState[kVlans] = vlans->toFollyDynamic();
  switchState[kRouteTables] = routeTables->toFollyDynamic();
  switchState[kAcls] = acls->toFollyDynamic();
  switchState[kDefaultVlan] = static_cast<uint32_t>(defaultVlan);
  return switchState;
}

SwitchStateFields
SwitchStateFields::fromFollyDynamic(const folly::dynamic& swJson) {
  SwitchStateFields switchState;
  switchState.interfaces = InterfaceMap::fromFollyDynamic(
        swJson[kInterfaces]);
  switchState.ports = PortMap::fromFollyDynamic(swJson[kPorts]);
  switchState.vlans = VlanMap::fromFollyDynamic(swJson[kVlans]);
  switchState.routeTables = RouteTableMap::fromFollyDynamic(
      swJson[kRouteTables]);
  switchState.acls = AclMap::fromFollyDynamic(swJson[kAcls]);
  switchState.defaultVlan = VlanID(swJson[kDefaultVlan].asInt());
  //TODO verify that created state here is internally consistent t4155406
  return switchState;
}

SwitchState::SwitchState() {
}

SwitchState::~SwitchState() {
}

void SwitchState::modify(std::shared_ptr<SwitchState>* state) {
  if (!(*state)->isPublished()) {
    return;
  }
  *state = (*state)->clone();
}

std::shared_ptr<Port> SwitchState::getPort(PortID id) const {
  return getFields()->ports->getPort(id);
}

void SwitchState::registerPort(PortID id, const std::string& name) {
  writableFields()->ports->registerPort(id, name);
}

void SwitchState::resetPorts(std::shared_ptr<PortMap> ports) {
  writableFields()->ports.swap(ports);
}

void SwitchState::resetVlans(std::shared_ptr<VlanMap> vlans) {
  writableFields()->vlans.swap(vlans);
}

void SwitchState::addVlan(const std::shared_ptr<Vlan>& vlan) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the VlanMap if we are still
  // pointing to a published map.
  if (fields->vlans->isPublished()) {
    fields->vlans = fields->vlans->clone();
  }
  fields->vlans->addVlan(vlan);
}

void SwitchState::setDefaultVlan(VlanID id) {
  writableFields()->defaultVlan = id;
}

void SwitchState::setArpTimeout(seconds timeout) {
  writableFields()->arpTimeout = timeout;
}

void SwitchState::setNdpTimeout(seconds timeout) {
  writableFields()->ndpTimeout = timeout;
}

void SwitchState::setArpAgerInterval(seconds interval) {
  writableFields()->arpAgerInterval = interval;
}

void SwitchState::setMaxNeighborProbes(uint32_t maxNeighborProbes) {
  writableFields()->maxNeighborProbes = maxNeighborProbes;
}

void SwitchState::setStaleEntryInterval(seconds interval) {
  writableFields()->staleEntryInterval = interval;
}

void SwitchState::addIntf(const std::shared_ptr<Interface>& intf) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the InterfaceMap if we are still
  // pointing to a published map.
  if (fields->interfaces->isPublished()) {
    fields->interfaces = fields->interfaces->clone();
  }
  fields->interfaces->addInterface(intf);
}

void SwitchState::resetIntfs(std::shared_ptr<InterfaceMap> intfs) {
  writableFields()->interfaces.swap(intfs);
}

void SwitchState::addRouteTable(const std::shared_ptr<RouteTable>& rt) {
  writableFields()->routeTables->addRouteTable(rt);
}

void SwitchState::resetRouteTables(std::shared_ptr<RouteTableMap> rts) {
  writableFields()->routeTables.swap(rts);
}

void SwitchState::addAcl(const std::shared_ptr<AclEntry>& acl) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the AclMap if we are still
  // pointing to a published map.
  if (fields->acls->isPublished()) {
    fields->acls = fields->acls->clone();
  }
  fields->acls->addEntry(acl);
}

std::shared_ptr<AclEntry> SwitchState::getAcl(AclEntryID id) const {
  return getFields()->acls->getEntryIf(id);
}

void SwitchState::resetAcls(std::shared_ptr<AclMap> acls) {
  writableFields()->acls.swap(acls);
}

void SwitchState::resetAggregatePorts(
    std::shared_ptr<AggregatePortMap> aggPorts) {
  writableFields()->aggPorts.swap(aggPorts);
}

template class NodeBaseT<SwitchState, SwitchStateFields>;

}} // facebook::fboss
