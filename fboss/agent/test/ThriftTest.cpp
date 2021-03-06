/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/state/RouteUpdater.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/state/Route.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::StringPiece;
using std::unique_ptr;
using std::shared_ptr;
using testing::UnorderedElementsAreArray;
using facebook::network::toBinaryAddress;
using cfg::PortSpeed;

namespace {

unique_ptr<SwSwitch> setupSwitch() {
  auto state = testStateA();
  auto sw = createMockSw(state);
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  return sw;
}

IpPrefix ipPrefix(StringPiece ip, int length) {
  IpPrefix result;
  result.ip = toBinaryAddress(IPAddress(ip));
  result.prefixLength = length;
  return result;
}

} // unnamed namespace

TEST(ThriftTest, getInterfaceDetail) {
  auto sw = setupSwitch();
  ThriftHandler handler(sw.get());

  // Query the two interfaces configured by testStateA()
  InterfaceDetail info;
  handler.getInterfaceDetail(info, 1);
  EXPECT_EQ("interface1", info.interfaceName);
  EXPECT_EQ(1, info.interfaceId);
  EXPECT_EQ(1, info.vlanId);
  EXPECT_EQ(0, info.routerId);
  EXPECT_EQ("00:02:00:00:00:01", info.mac);
  std::vector<IpPrefix> expectedAddrs = {
    ipPrefix("10.0.0.1", 24),
    ipPrefix("192.168.0.1", 24),
    ipPrefix("2401:db00:2110:3001::0001", 64),
  };
  EXPECT_THAT(info.address, UnorderedElementsAreArray(expectedAddrs));

  handler.getInterfaceDetail(info, 55);
  EXPECT_EQ("interface55", info.interfaceName);
  EXPECT_EQ(55, info.interfaceId);
  EXPECT_EQ(55, info.vlanId);
  EXPECT_EQ(0, info.routerId);
  EXPECT_EQ("00:02:00:00:00:55", info.mac);
  expectedAddrs = {
    ipPrefix("10.0.55.1", 24),
    ipPrefix("192.168.55.1", 24),
    ipPrefix("2401:db00:2110:3055::0001", 64),
  };
  EXPECT_THAT(info.address, UnorderedElementsAreArray(expectedAddrs));

  // Calling getInterfaceDetail() on an unknown
  // interface should throw an FbossError.
  EXPECT_THROW(handler.getInterfaceDetail(info, 123), FbossError);
}


TEST(ThriftTest, assertPortSpeeds) {
  // We rely on the exact value of the port speeds for some
  // logic, so we want to ensure that these values don't change.
  EXPECT_EQ(static_cast<int>(PortSpeed::GIGE), 1000);
  EXPECT_EQ(static_cast<int>(PortSpeed::XG), 10000);
  EXPECT_EQ(static_cast<int>(PortSpeed::TWENTYG), 20000);
  EXPECT_EQ(static_cast<int>(PortSpeed::TWENTYFIVEG), 25000);
  EXPECT_EQ(static_cast<int>(PortSpeed::FORTYG), 40000);
  EXPECT_EQ(static_cast<int>(PortSpeed::FIFTYG), 50000);
  EXPECT_EQ(static_cast<int>(PortSpeed::HUNDREDG), 100000);
}

TEST(ThriftTest, LinkLocalRoutes) {
  auto platform = createMockPlatform();
  auto stateV0 = testStateB();
  // Remove all linklocalroutes from stateV0 in order to clear all
  // linklocalroutes
  RouteUpdater updater(stateV0->getRouteTables());
  updater.delLinkLocalRoutes(RouterID(0));
  auto newRt = updater.updateDone();
  stateV0->resetRouteTables(newRt);
  cfg::SwitchConfig config;
  config.vlans.resize(1);
  config.vlans[0].id = 1;
  config.interfaces.resize(1);
  config.interfaces[0].intfID = 1;
  config.interfaces[0].vlanID = 1;
  config.interfaces[0].routerID = 0;
  config.interfaces[0].__isset.mac = true;
  config.interfaces[0].mac = "00:02:00:00:00:01";
  config.interfaces[0].ipAddresses.resize(3);
  config.interfaces[0].ipAddresses[0] = "10.0.0.1/24";
  config.interfaces[0].ipAddresses[1] = "192.168.0.1/24";
  config.interfaces[0].ipAddresses[2] = "2401:db00:2110:3001::0001/64";
  // Call applyThriftConfig
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  stateV1->publish();
  // Verify that stateV1 contains the link local route
  shared_ptr<RouteTable> rt = stateV1->getRouteTables()->
                              getRouteTableIf(RouterID(0));
  ASSERT_NE(nullptr, rt);
  // Link local addr.
  auto ip = IPAddressV6("fe80::");
  // Find longest match to link local addr.
  auto longestMatchRoute = rt->getRibV6()->longestMatch(ip);
  // Verify that a route is found
  ASSERT_NE(nullptr, longestMatchRoute);
  // Verify that the route is to link local addr.
  ASSERT_EQ(longestMatchRoute->prefix().network, ip);
}

std::unique_ptr<UnicastRoute>
makeUnicastRoute(std::string prefixStr, std::string nxtHop) {
  std::vector<std::string> vec;
  folly::split("/", prefixStr, vec);
  EXPECT_EQ(2, vec.size());
  auto nr = std::make_unique<UnicastRoute>();
  nr->dest.ip = toBinaryAddress(IPAddress(vec.at(0)));
  nr->dest.prefixLength = folly::to<uint8_t>(vec.at(1));
  nr->nextHopAddrs.push_back(toBinaryAddress(IPAddress(nxtHop)));
  return nr;
}

// Test for the ThriftHandler::syncFib method
TEST(ThriftTest, syncFib) {
  RouterID rid = RouterID(0);

  // Create a config
  cfg::SwitchConfig config;
  config.vlans.resize(1);
  config.vlans[0].id = 1;
  config.interfaces.resize(1);
  config.interfaces[0].intfID = 1;
  config.interfaces[0].vlanID = 1;
  config.interfaces[0].routerID = 0;
  config.interfaces[0].__isset.mac = true;
  config.interfaces[0].mac = "00:02:00:00:00:01";
  config.interfaces[0].ipAddresses.resize(3);
  config.interfaces[0].ipAddresses[0] = "10.0.0.1/24";
  config.interfaces[0].ipAddresses[1] = "192.168.0.19/24";
  config.interfaces[0].ipAddresses[2] = "2401:db00:2110:3001::0001/64";

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  auto mockSw = createMockSw(&config);
  mockSw->initialConfigApplied(std::chrono::steady_clock::now());
  mockSw->fibSynced();
  ThriftHandler handler(mockSw.get());

  //
  // Add a few BGP routes
  //

  auto cli1_nhop4 = "11.11.11.11";
  auto cli1_nhop6 = "11:11::0";
  auto cli2_nhop4 = "22.22.22.22";
  auto cli2_nhop6 = "22:22::0";
  auto cli3_nhop6 = "33:33::0";
  auto cli1_nhop6b = "44:44::0";

  // These routes will include nexthops from client 1 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  handler.addUnicastRoute(1, makeUnicastRoute(prefixA4, cli1_nhop4));
  handler.addUnicastRoute(1, makeUnicastRoute(prefixA6, cli1_nhop6));

  // This route will include nexthops from clients 1 and 2
  auto prefixB4 = "7.2.0.0/16";
  handler.addUnicastRoute(1, makeUnicastRoute(prefixB4, cli1_nhop4));
  handler.addUnicastRoute(2, makeUnicastRoute(prefixB4, cli2_nhop4));

  // This route will include nexthops from clients 1 and 2 and 3
  auto prefixC6 = "aaaa:3::0/64";
  handler.addUnicastRoute(1, makeUnicastRoute(prefixC6, cli1_nhop6));
  handler.addUnicastRoute(2, makeUnicastRoute(prefixC6, cli2_nhop6));
  handler.addUnicastRoute(3, makeUnicastRoute(prefixC6, cli3_nhop6));

  // These routes will not be used until fibSync happens.
  auto prefixD4 = "7.4.0.0/16";
  auto prefixD6 = "aaaa:4::0/64";

  //
  // Test the state of things before calling syncFib
  //

  // Make sure all the static and link-local routes are there
  auto tables2 = handler.getSw()->getState()->getRouteTables();
  GET_ROUTE_V4(tables2, rid, "10.0.0.0/24");
  GET_ROUTE_V4(tables2, rid, "192.168.0.0/24");
  GET_ROUTE_V6(tables2, rid, "2401:db00:2110:3001::/64");
  GET_ROUTE_V6(tables2, rid, "fe80::/64");
  // Make sure the client 1&2&3 routes are there.
  GET_ROUTE_V4(tables2, rid, prefixA4);
  GET_ROUTE_V6(tables2, rid, prefixA6);
  GET_ROUTE_V4(tables2, rid, prefixB4);
  GET_ROUTE_V6(tables2, rid, prefixC6);
  // Make sure there are no more routes than the ones we just tested
  EXPECT_EQ(4, tables2->getRouteTable(rid)->getRibV4()->size());
  EXPECT_EQ(4, tables2->getRouteTable(rid)->getRibV6()->size());
  EXPECT_NO_ROUTE(tables2, rid, prefixD4);
  EXPECT_NO_ROUTE(tables2, rid, prefixD6);

  //
  // Now use syncFib to remove all the routes for client 1 and add some new ones
  // Statics, link-locals, and clients 2 and 3 should remain unchanged.
  //

  auto newRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr1 = *makeUnicastRoute(prefixC6, cli1_nhop6b).get();
  UnicastRoute nr2 = *makeUnicastRoute(prefixD6, cli1_nhop6b).get();
  UnicastRoute nr3 = *makeUnicastRoute(prefixD4, cli1_nhop4).get();
  newRoutes->push_back(nr1);
  newRoutes->push_back(nr2);
  newRoutes->push_back(nr3);
  handler.syncFib(1, std::move(newRoutes));

  //
  // Test the state of things after syncFib
  //

  // Make sure all the static and link-local routes are still there
  auto tables3 = handler.getSw()->getState()->getRouteTables();
  GET_ROUTE_V4(tables3, rid, "10.0.0.0/24");
  GET_ROUTE_V4(tables3, rid, "192.168.0.0/24");
  GET_ROUTE_V6(tables3, rid, "2401:db00:2110:3001::/64");
  GET_ROUTE_V6(tables3, rid, "fe80::/64");

  // The prefixA* routes should have disappeared
  EXPECT_NO_ROUTE(tables3, rid, prefixA4);
  EXPECT_NO_ROUTE(tables3, rid, prefixA6);

  // The prefixB4 route should have client 2 only
  auto rt1 = GET_ROUTE_V4(tables3, rid, prefixB4);
  ASSERT_TRUE(rt1->getFields()
    ->nexthopsmulti.isSame(ClientID(2), makeNextHops({cli2_nhop4})));
  auto bestNextHops = rt1->bestNextHopList();
  EXPECT_EQ(IPAddress(cli2_nhop4), bestNextHops.begin()->addr());

  // The prefixC6 route should have clients 2 & 3, and a new value for client 1
  auto rt2 = GET_ROUTE_V6(tables3, rid, prefixC6);
  ASSERT_TRUE(rt2->getFields()
    ->nexthopsmulti.isSame(ClientID(2), makeNextHops({cli2_nhop6})));
  ASSERT_TRUE(rt2->getFields()
    ->nexthopsmulti.isSame(ClientID(3), makeNextHops({cli3_nhop6})));
  ASSERT_TRUE(rt2->getFields()
    ->nexthopsmulti.isSame(ClientID(1), makeNextHops({cli1_nhop6b})));

  // The prefixD4 and prefixD6 routes should have been created
  GET_ROUTE_V4(tables3, rid, prefixD4);
  GET_ROUTE_V6(tables3, rid, prefixD6);

  // Make sure there are no more routes (ie. the old ones were deleted)
  EXPECT_EQ(4, tables3->getRouteTable(rid)->getRibV4()->size());
  EXPECT_EQ(4, tables3->getRouteTable(rid)->getRibV6()->size());
}
