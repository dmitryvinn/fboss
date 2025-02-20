/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiFdbManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiBridgeManager.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/MacEntry.h"

#include <memory>
#include <tuple>

namespace facebook::fboss {

SaiFdbManager::SaiFdbManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform,
    const ConcurrentIndices* concurrentIndices)
    : saiStore_(saiStore),
      managerTable_(managerTable),
      platform_(platform),
      concurrentIndices_(concurrentIndices) {}

void ManagedFdbEntry::createObject(PublisherObjects objects) {
  /* bridge port exist, create fdb entry */
  auto vlan = SaiApiTable::getInstance()->routerInterfaceApi().getAttribute(
      std::get<RouterInterfaceSaiId>(saiPortAndIntf_),
      SaiVlanRouterInterfaceTraits::Attributes::VlanId{});
  SaiFdbTraits::FdbEntry entry{switchId_, vlan, getMac()};

  auto bridgePort = std::get<BridgePortWeakPtr>(objects).lock();
  auto bridgePortId = bridgePort->adapterKey();
  SaiFdbTraits::CreateAttributes attributes{type_, bridgePortId, metadata_};

  auto fdbEntry = manager_->createSaiObject(entry, attributes, intfIDAndMac_);
  // For FDB entry, on delete, Ignore error if entry was already removed from
  // HW. One scenario where this can occur is the following
  // - We learn a MAC and install it in HW
  // - Later we get a state update to transform this into a STATIC FDB entry
  // - Meanwhile the dynamic MAC ages out and gets deleted
  // - While processing the state delta for changed MAC entry, we try to
  // delete the dynamic entry before adding static entry
  fdbEntry->setIgnoreMissingInHwOnDelete(true);
  setObject(fdbEntry);
  XLOG(DBG2) << "ManagedFdbEntry::createObject: " << toL2Entry().str();
}

void ManagedFdbEntry::removeObject(size_t, PublisherObjects) {
  XLOG(DBG2) << "ManagedFdbEntry::removeObject: " << toL2Entry().str();
  /* either interface is removed or bridge port is removed, delete fdb entry */
  this->resetObject();
}

SaiPortDescriptor ManagedFdbEntry::getPortId() const {
  return std::get<SaiPortDescriptor>(saiPortAndIntf_);
}

L2Entry ManagedFdbEntry::toL2Entry() const {
  std::optional<cfg::AclLookupClass> classId;
  if (metadata_) {
    classId = static_cast<cfg::AclLookupClass>(metadata_.value());
  }
  auto port = getPortId();
  auto mac = getMac();
  return L2Entry(
      mac,
      // For FBOSS Vlan and interface ids are always 1:1
      VlanID(getInterfaceID()),
      port.isPhysicalPort() ? PortDescriptor(port.phyPortID())
                            : PortDescriptor(port.aggPortID()),
      // Since this entry is already programmed, its validated.
      // In vlan traffic to this MAC will be unicast and not
      // flooded.
      L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED,
      classId);
}

SaiFdbTraits::FdbEntry ManagedFdbEntry::makeFdbEntry(
    const SaiManagerTable* managerTable) const {
  auto rifHandle =
      managerTable->routerInterfaceManager().getRouterInterfaceHandle(
          getInterfaceID());
  auto vlan = GET_ATTR(
      VlanRouterInterface, VlanId, rifHandle->routerInterface->attributes());
  return SaiFdbTraits::FdbEntry{switchId_, vlan, getMac()};
}

void ManagedFdbEntry::handleLinkDown() {
  XLOG(DBG2) << "fdb entry (" << getInterfaceID() << ", " << getMac().toString()
             << ") notifying link down to subscribed neighbors";
  SaiObjectEventPublisher::getInstance()->get<SaiFdbTraits>().notifyLinkDown(
      intfIDAndMac_);
}

std::string ManagedFdbEntry::toString() const {
  auto saiPortDescStr = std::get<SaiPortDescriptor>(saiPortAndIntf_).str();
  auto intfIdStr = std::to_string(std::get<InterfaceID>(intfIDAndMac_));
  auto macStr = std::get<folly::MacAddress>(intfIDAndMac_).toString();
  auto typeStr = std::to_string(type_);
  auto metaDataStr = metadata_ ? std::to_string(metadata_.value()) : "none";

  return folly::to<std::string>(
      getObject() ? "active " : "inactive ",
      "managed fdb entry: ",
      "port: ",
      saiPortDescStr,
      ", interface: ",
      intfIdStr,
      ", mac: ",
      macStr,
      ", type: ",
      typeStr,
      ", metadata: ",
      metaDataStr);
}

void SaiFdbManager::addFdbEntry(
    SaiPortDescriptor port,
    InterfaceID interfaceId,
    folly::MacAddress mac,
    sai_fdb_entry_type_t type,
    std::optional<sai_uint32_t> metadata) {
  XLOGF(
      INFO,
      "Add fdb entry {}, {}, {}, type: {}, metadata: {}",
      port.str(),
      interfaceId,
      mac,
      (type == SAI_FDB_ENTRY_TYPE_STATIC ? "static" : "dynamic"),
      metadata ? metadata.value() : 0);

  auto switchId = managerTable_->switchManager().getSwitchSaiId();
  auto key = PublisherKey<SaiFdbTraits>::custom_type{interfaceId, mac};
  auto managedFdbEntryIter = managedFdbEntries_.find(key);
  if (managedFdbEntryIter != managedFdbEntries_.end()) {
    XLOGF(
        INFO,
        "fdb entry {}, {}, {}, type: {}, metadata: {} already exists.",
        managedFdbEntryIter->second->getPortId().str(),
        managedFdbEntryIter->second->getInterfaceID(),
        managedFdbEntryIter->second->getMac(),
        (managedFdbEntryIter->second->getType() == SAI_FDB_ENTRY_TYPE_STATIC
             ? "static"
             : "dynamic"),
        managedFdbEntryIter->second->getMetaData());
    return;
  }
  auto saiRouterIntf =
      managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
          interfaceId);
  auto managedFdbEntry = std::make_shared<ManagedFdbEntry>(
      this,
      switchId,
      std::make_tuple(port, saiRouterIntf->routerInterface->adapterKey()),
      std::make_tuple(interfaceId, mac),
      type,
      metadata);

  SaiObjectEventPublisher::getInstance()->get<SaiBridgePortTraits>().subscribe(
      managedFdbEntry);
  portToKeys_[port].emplace(key);
  managedFdbEntries_.emplace(key, managedFdbEntry);
}

void ManagedFdbEntry::update(const std::shared_ptr<MacEntry>& updated) {
  if (updated->getPort().isPhysicalPort()) {
    CHECK_EQ(getPortId(), updated->getPort().phyPortID());
  } else {
    CHECK_EQ(getPortId(), updated->getPort().aggPortID());
  }
  CHECK_EQ(getMac(), updated->getMac());
  auto fdbEntry = getSaiObject();
  CHECK(fdbEntry != nullptr) << "updating non-programmed fdb entry";
  fdbEntry->setAttribute(SaiFdbTraits::Attributes::Type(
      updated->getType() == MacEntryType::STATIC_ENTRY
          ? SAI_FDB_ENTRY_TYPE_STATIC
          : SAI_FDB_ENTRY_TYPE_DYNAMIC));
  sai_uint32_t metadata = updated->getClassID()
      ? static_cast<sai_uint32_t>(updated->getClassID().value())
      : 0;
  fdbEntry->setOptionalAttribute(SaiFdbTraits::Attributes::Metadata{metadata});
}

void SaiFdbManager::removeFdbEntry(
    InterfaceID interfaceId,
    folly::MacAddress mac) {
  XLOGF(INFO, "Remove fdb entry {}, {}", interfaceId, mac);
  auto key = PublisherKey<SaiFdbTraits>::custom_type{interfaceId, mac};
  auto fdbEntryItr = managedFdbEntries_.find(key);
  if (fdbEntryItr == managedFdbEntries_.end()) {
    XLOG(WARN) << "Attempted to remove non-existent FDB entry";
    return;
  }
  auto portId = fdbEntryItr->second->getPortId();
  portToKeys_[portId].erase(key);
  if (portToKeys_[portId].empty()) {
    portToKeys_.erase(portId);
  }
  managedFdbEntries_.erase(fdbEntryItr);
}

void SaiFdbManager::addMac(const std::shared_ptr<MacEntry>& macEntry) {
  std::optional<sai_uint32_t> metadata{std::nullopt};
  if (macEntry->getClassID()) {
    metadata = static_cast<sai_uint32_t>(macEntry->getClassID().value());
  }
  auto type = SAI_FDB_ENTRY_TYPE_DYNAMIC;
  switch (macEntry->getType()) {
    case MacEntryType::STATIC_ENTRY:
      type = SAI_FDB_ENTRY_TYPE_STATIC;
      break;
    case MacEntryType::DYNAMIC_ENTRY:
      type = SAI_FDB_ENTRY_TYPE_DYNAMIC;
      break;
  };
  addFdbEntry(
      macEntry->getPort().isPhysicalPort()
          ? SaiPortDescriptor(macEntry->getPort().phyPortID())
          : SaiPortDescriptor(macEntry->getPort().aggPortID()),
      getInterfaceId(macEntry),
      macEntry->getMac(),
      type,
      metadata);
}

void SaiFdbManager::removeMac(const std::shared_ptr<MacEntry>& macEntry) {
  removeFdbEntry(getInterfaceId(macEntry), macEntry->getMac());
}

void SaiFdbManager::changeMac(
    const std::shared_ptr<MacEntry>& oldEntry,
    const std::shared_ptr<MacEntry>& newEntry) {
  if (*oldEntry != *newEntry) {
    CHECK_EQ(oldEntry->getMac(), newEntry->getMac());
    auto newIntfId = getInterfaceId(newEntry);
    auto key =
        PublisherKey<SaiFdbTraits>::custom_type{newIntfId, newEntry->getMac()};
    auto iter = managedFdbEntries_.find(key);
    CHECK(iter != managedFdbEntries_.end())
        << "updating non-existing mac entry";

    if (oldEntry->getPort() != newEntry->getPort() ||
        !iter->second->isAlive()) {
      removeMac(oldEntry);
      addMac(newEntry);
    } else {
      XLOGF(INFO, "Change fdb entry {}, {}", oldEntry->str(), newEntry->str());
      try {
        iter->second->update(newEntry);
      } catch (const SaiApiError& e) {
        // For change of MAC entry there is a possibility that the
        // previous entry was dynamic and got aged out
        if (oldEntry->getType() == MacEntryType::DYNAMIC_ENTRY &&
            e.getSaiStatus() == SAI_STATUS_ITEM_NOT_FOUND) {
          removeMac(oldEntry);
          addMac(newEntry);
        } else {
          throw;
        }
      }
    }
  }
}

PortDescriptorSaiId SaiFdbManager::getPortDescriptorSaiId(
    const PortDescriptor& portDesc) const {
  if (portDesc.isPhysicalPort()) {
    auto const portHandle =
        managerTable_->portManager().getPortHandle(portDesc.phyPortID());
    return PortDescriptorSaiId(portHandle->port->adapterKey());
  }
  auto lagHandle =
      managerTable_->lagManager().getLagHandle(portDesc.aggPortID());
  return PortDescriptorSaiId(lagHandle->lag->adapterKey());
}

InterfaceID SaiFdbManager::getInterfaceId(
    const std::shared_ptr<MacEntry>& macEntry) const {
  // FBOSS assumes a 1:1 correpondance b/w Vlan and interfae IDs
  auto portDescSaiId = getPortDescriptorSaiId(macEntry->getPort());
  return InterfaceID(concurrentIndices_->vlanIds.find(portDescSaiId)->second);
}

void SaiFdbManager::handleLinkDown(SaiPortDescriptor portId) {
  auto portToKeysItr = portToKeys_.find(portId);
  if (portToKeysItr != portToKeys_.end()) {
    for (const auto& key : portToKeysItr->second) {
      auto fdbEntryItr = managedFdbEntries_.find(key);
      if (UNLIKELY(fdbEntryItr == managedFdbEntries_.end())) {
        XLOGF(
            FATAL,
            "no fdb entry found for key in portToKeys_ mapping: {}",
            key);
      }
      /*
       * notify link down event which propagate all the way to next hop group
       * and next hop. next hop is removed and next hop group shrinks.
       *
       * instead of resetting fdb entry, propagate link down event. fdb entry
       * (if static and related to neighbor) will  be purged  by  sw switch's
       * neighbor updater mechanism.
       *
       * this retains hw switch in sync with sw switch
       */
      fdbEntryItr->second->handleLinkDown();
    }
  }
}

L2EntryThrift SaiFdbManager::fdbToL2Entry(
    const SaiFdbTraits::FdbEntry& fdbEntry) const {
  L2EntryThrift entry;
  // SwitchState's VlanID is an attribute we store in the vlan, so
  // we can get it via SaiApi
  auto& vlanApi = SaiApiTable::getInstance()->vlanApi();
  VlanID swVlanId{vlanApi.getAttribute(
      VlanSaiId{fdbEntry.bridgeVlanId()}, SaiVlanTraits::Attributes::VlanId{})};
  entry.vlanID_ref() = swVlanId;
  entry.mac_ref() = fdbEntry.mac().toString();
  // If we programmed a entry via SaiFdbManager,
  // its valiadted
  entry.l2EntryType_ref() = L2EntryType::L2_ENTRY_TYPE_VALIDATED;
  entry.port_ref() = 0;

  // To get the PortID, we get the bridgePortId from the fdb entry,
  // then get that Bridge Port's PortId attribute. We can lookup the
  // PortID for a sai port id in ConcurrentIndices
  auto& fdbApi = SaiApiTable::getInstance()->fdbApi();
  auto bridgePortSaiId =
      fdbApi.getAttribute(fdbEntry, SaiFdbTraits::Attributes::BridgePortId());
  auto& bridgeApi = SaiApiTable::getInstance()->bridgeApi();
  auto portOrLagSaiId = bridgeApi.getAttribute(
      BridgePortSaiId{bridgePortSaiId},
      SaiBridgePortTraits::Attributes::PortId{});
  const auto portItr =
      concurrentIndices_->portIds.find(PortSaiId{portOrLagSaiId});
  const auto lagItr =
      concurrentIndices_->aggregatePortIds.find(LagSaiId{portOrLagSaiId});

  if (portItr != concurrentIndices_->portIds.cend()) {
    entry.port_ref() = portItr->second;
  } else if (lagItr != concurrentIndices_->aggregatePortIds.cend()) {
    entry.trunk_ref() = lagItr->second;
  } else {
    throw FbossError(
        "l2 table entry had unknown port sai id: ", portOrLagSaiId);
  }
  auto metadata =
      fdbApi.getAttribute(fdbEntry, SaiFdbTraits::Attributes::Metadata{});
  if (metadata) {
    entry.classID_ref() = metadata;
  }
  return entry;
}

std::vector<L2EntryThrift> SaiFdbManager::getL2Entries() const {
  std::vector<L2EntryThrift> entries;
  entries.reserve(managedFdbEntries_.size());
  for (const auto& publisherAndFdbEntry : managedFdbEntries_) {
    entries.emplace_back(
        fdbToL2Entry(publisherAndFdbEntry.second->makeFdbEntry(managerTable_)));
  }
  return entries;
}

std::shared_ptr<SaiFdbEntry> SaiFdbManager::createSaiObject(
    const typename SaiFdbTraits::AdapterHostKey& key,
    const typename SaiFdbTraits::CreateAttributes& attributes,
    const PublisherKey<SaiFdbTraits>::custom_type& publisherKey) {
  auto& store = saiStore_->get<SaiFdbTraits>();
  return store.setObject(key, attributes, publisherKey);
}

void SaiFdbManager::removeUnclaimedDynanicEntries() {
  auto l2LearningMode = managerTable_->bridgeManager().getL2LearningMode();
  if (l2LearningMode == cfg::L2LearningMode::SOFTWARE) {
    return;
  }
  // in hardware learning, switch state will not hold corresponding
  // mac table entry for dynamic entries. because learning events are
  // not dispatched to software switch.
  // in software learning, switch state will hold corresponding mac table
  // entry for dynamic entries. because learning events are are always
  // dispatched to software switch.
  // static fdb entries must always be claimed from switch state.
  // dynamic entries may be discovered both during cold boot and warm boot
  // for warm boot, dynamic entries may exist if agent warmboots before entries
  // expire. for cold boot, learning begins as soon as ports come up and store
  // is reloaded even on cold boot.
  saiStore_->get<SaiFdbTraits>().removeUnclaimedWarmbootHandlesIf(
      [](const auto& fdbEntry) {
        auto type =
            std::get<SaiFdbTraits::Attributes::Type>(fdbEntry->attributes());
        if (type.value() != SAI_FDB_ENTRY_TYPE_DYNAMIC) {
          return false;
        }
        // do not attempt to delete in hardware but remove from store.
        fdbEntry->release();
        return true;
      });
}

std::string SaiFdbManager::listManagedObjects() const {
  std::string output{};

  for (auto entry : managedFdbEntries_) {
    output += entry.second->toString();
    output += "\n";
  }
  return output;
}
} // namespace facebook::fboss
