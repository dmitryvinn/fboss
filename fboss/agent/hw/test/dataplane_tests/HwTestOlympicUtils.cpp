/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/test/dataplane_tests/HwTestOlympicUtils.h"

#include "fboss/agent/hw/switch_asics/HwAsic.h"

namespace facebook::fboss::utility {

cfg::ActiveQueueManagement kGetOlympicEcnConfig(int minLength, int maxLength) {
  cfg::ActiveQueueManagement ecnAQM;
  cfg::LinearQueueCongestionDetection ecnLQCD;
  ecnLQCD.minimumLength_ref() = minLength;
  ecnLQCD.maximumLength_ref() = maxLength;
  ecnAQM.detection_ref()->linear_ref() = ecnLQCD;
  ecnAQM.behavior_ref() = cfg::QueueCongestionBehavior::ECN;
  return ecnAQM;
}

cfg::ActiveQueueManagement
kGetWredConfig(int minLength, int maxLength, int probability) {
  cfg::ActiveQueueManagement wredAQM;
  cfg::LinearQueueCongestionDetection wredLQCD;
  wredLQCD.minimumLength_ref() = minLength;
  wredLQCD.maximumLength_ref() = maxLength;
  wredLQCD.probability_ref() = probability;
  wredAQM.detection_ref()->linear_ref() = wredLQCD;
  wredAQM.behavior_ref() = cfg::QueueCongestionBehavior::EARLY_DROP;
  return wredAQM;
}

void addQueueShaperConfig(
    cfg::SwitchConfig* config,
    const int queueId,
    const uint32_t minKbps,
    const uint32_t maxKbps) {
  cfg::Range kbpsRange;
  kbpsRange.minimum_ref() = minKbps;
  kbpsRange.maximum_ref() = maxKbps;
  auto& queue = config->portQueueConfigs_ref()["queue_config"][queueId];
  queue.portQueueRate_ref() = cfg::PortQueueRate();
  queue.portQueueRate_ref()->kbitsPerSec_ref() = kbpsRange;
}

void addQueueBurstSizeConfig(
    cfg::SwitchConfig* config,
    const int queueId,
    const uint32_t minKbits,
    const uint32_t maxKbits) {
  auto& queue = config->portQueueConfigs_ref()["queue_config"][queueId];
  queue.bandwidthBurstMinKbits_ref() = minKbits;
  queue.bandwidthBurstMaxKbits_ref() = maxKbits;
}

void addQueueEcnConfig(
    cfg::SwitchConfig* config,
    const int queueId,
    const uint32_t minLen,
    const uint32_t maxLen) {
  auto& queue = config->portQueueConfigs_ref()["queue_config"][queueId];
  if (!queue.aqms_ref().has_value()) {
    queue.aqms_ref() = {};
  }
  queue.aqms_ref()->push_back(kGetOlympicEcnConfig(minLen, maxLen));
}

void addQueueWredConfig(
    cfg::SwitchConfig* config,
    const int queueId,
    const uint32_t minLen,
    const uint32_t maxLen,
    const int probability) {
  auto& queue = config->portQueueConfigs_ref()["queue_config"][queueId];
  if (!queue.aqms_ref().has_value()) {
    queue.aqms_ref() = {};
  }
  queue.aqms_ref()->push_back(kGetWredConfig(minLen, maxLen, probability));
}

void addNetworkAIQueueConfig(
    cfg::SwitchConfig* config,
    cfg::StreamType streamType) {
  std::vector<cfg::PortQueue> portQueues;

  cfg::PortQueue queue0;
  queue0.id_ref() = kNetworkAIMonitoringQueueId;
  queue0.name_ref() = "queue0.monitoring";
  queue0.streamType_ref() = streamType;
  queue0.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
  portQueues.push_back(queue0);

  cfg::PortQueue queue1;
  queue0.id_ref() = kNetworkAIRdmaQueueId;
  queue0.name_ref() = "queue6.rdma";
  queue0.streamType_ref() = streamType;
  queue0.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
  portQueues.push_back(queue1);

  cfg::PortQueue queue2;
  queue0.id_ref() = kNetworkAINCQueueId;
  queue0.name_ref() = "queue7.nc";
  queue0.streamType_ref() = streamType;
  queue0.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
  portQueues.push_back(queue2);

  config->portQueueConfigs_ref()["queue_config"] = portQueues;
  for (auto& port : *config->ports_ref()) {
    port.portQueueConfigName_ref() = "queue_config";
  }
}

// XXX This is FSW config, add RSW config. Prefix queue names with portName
void addOlympicQueueConfig(
    cfg::SwitchConfig* config,
    cfg::StreamType streamType,
    const HwAsic* asic,
    bool addWredConfig) {
  std::vector<cfg::PortQueue> portQueues;

  cfg::PortQueue queue0;
  *queue0.id_ref() = kOlympicSilverQueueId;
  queue0.name_ref() = "queue0.silver";
  queue0.streamType_ref() = streamType;
  *queue0.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue0.weight_ref() = kOlympicSilverWeight;
  queue0.scalingFactor_ref() = cfg::MMUScalingFactor::ONE;
  if (!asic->mmuQgroupsEnabled()) {
    queue0.reservedBytes_ref() = 3328;
  }
  portQueues.push_back(queue0);

  cfg::PortQueue queue1;
  *queue1.id_ref() = kOlympicGoldQueueId;
  queue1.name_ref() = "queue1.gold";
  queue1.streamType_ref() = streamType;
  *queue1.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue1.weight_ref() = kOlympicGoldWeight;
  queue1.scalingFactor_ref() = cfg::MMUScalingFactor::EIGHT;
  if (!asic->mmuQgroupsEnabled()) {
    queue1.reservedBytes_ref() = 9984;
  }
  portQueues.push_back(queue1);

  cfg::PortQueue queue2;
  *queue2.id_ref() = kOlympicEcn1QueueId;
  queue2.name_ref() = "queue2.ecn1";
  queue2.streamType_ref() = streamType;
  *queue2.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue2.weight_ref() = kOlympicEcn1Weight;
  queue2.scalingFactor_ref() = cfg::MMUScalingFactor::ONE;
  queue2.aqms_ref() = {};
  queue2.aqms_ref()->push_back(kGetOlympicEcnConfig());
  if (addWredConfig) {
    queue2.aqms_ref()->push_back(kGetWredConfig());
  }
  portQueues.push_back(queue2);

  cfg::PortQueue queue4;
  *queue4.id_ref() = kOlympicBronzeQueueId;
  queue4.name_ref() = "queue4.bronze";
  queue4.streamType_ref() = streamType;
  *queue4.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue4.weight_ref() = kOlympicBronzeWeight;
  portQueues.push_back(queue4);

  cfg::PortQueue queue6;
  *queue6.id_ref() = kOlympicICPQueueId;
  queue6.name_ref() = "queue6.platinum";
  queue6.streamType_ref() = streamType;
  *queue6.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
  if (!asic->mmuQgroupsEnabled()) {
    queue6.reservedBytes_ref() = 9984;
  }
  queue6.scalingFactor_ref() = cfg::MMUScalingFactor::EIGHT;
  portQueues.push_back(queue6);

  cfg::PortQueue queue7;
  *queue7.id_ref() = kOlympicNCQueueId;
  queue7.name_ref() = "queue7.network_control";
  queue7.streamType_ref() = streamType;
  *queue7.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
  portQueues.push_back(queue7);

  config->portQueueConfigs_ref()["queue_config"] = portQueues;
  for (auto& port : *config->ports_ref()) {
    port.portQueueConfigName_ref() = "queue_config";
  }
}

// Configure two queues (silver and ecn) with the same weight but different drop
// probability. It is used to verify the queue with higher drop probability will
// have more drops and lower watermark.
void addQueueWredDropConfig(
    cfg::SwitchConfig* config,
    cfg::StreamType streamType) {
  std::vector<cfg::PortQueue> portQueues;

  // 256 packets in the test, where each packet has a payload of 7000 bytes
  auto constexpr maxThresh = 7000 * 256;

  cfg::PortQueue queue0;
  queue0.id_ref() = kOlympicSilverQueueId;
  queue0.name_ref() = "queue0";
  queue0.streamType_ref() = streamType;
  queue0.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue0.weight_ref() = kOlympicEcn1Weight;
  queue0.scalingFactor_ref() = cfg::MMUScalingFactor::ONE;
  queue0.aqms_ref() = {};
  queue0.aqms_ref()->push_back(kGetWredConfig(0, maxThresh, 0));
  portQueues.push_back(queue0);

  cfg::PortQueue queue2;
  queue2.id_ref() = kOlympicEcn1QueueId;
  queue2.name_ref() = "queue2";
  queue2.streamType_ref() = streamType;
  queue2.scheduling_ref() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  queue2.weight_ref() = kOlympicEcn1Weight;
  queue2.scalingFactor_ref() = cfg::MMUScalingFactor::ONE;
  queue2.aqms_ref() = {};
  queue2.aqms_ref()->push_back(kGetWredConfig(0, maxThresh, 5));
  portQueues.push_back(queue2);

  config->portQueueConfigs_ref()["queue_config"] = portQueues;
  for (auto& port : *config->ports_ref()) {
    port.portQueueConfigName_ref() = "queue_config";
  }
}

const std::map<int, std::string>& kOlympicAllSPQueueIdToQueueName() {
  static const std::map<int, std::string> queueIdToQueueName = {
      {kOlympicAllSPNCNFQueueId, "queue0.ncnf"},
      {kOlympicAllSPBronzeQueueId, "queue1.bronze"},
      {kOlympicAllSPSilverQueueId, "queue2.silver"},
      {kOlympicAllSPGoldQueueId, "queue3.gold"},
      {kOlympicAllSPICPQueueId, "queeu6.icp"},
      {kOlympicAllSPNCQueueId, "queue7.nc"},
  };

  return queueIdToQueueName;
}

void addOlympicAllSPQueueConfig(
    cfg::SwitchConfig* config,
    cfg::StreamType streamType) {
  std::vector<cfg::PortQueue> portQueues;

  for (const auto& [queueId, queueName] : kOlympicAllSPQueueIdToQueueName()) {
    cfg::PortQueue queue;
    queue.id_ref() = queueId;
    queue.name_ref() = queueName;
    queue.streamType_ref() = streamType;
    queue.scheduling_ref() = cfg::QueueScheduling::STRICT_PRIORITY;
    portQueues.push_back(queue);
  }

  config->portQueueConfigs_ref()["queue_config"] = portQueues;
  for (auto& port : *config->ports_ref()) {
    port.portQueueConfigName_ref() = "queue_config";
  }
}

std::string getOlympicAclNameForDscp(uint8_t dscp) {
  return folly::to<std::string>("olympic_acl_dscp", dscp);
}

std::string getOlympicCounterNameForDscp(uint8_t dscp) {
  return folly::to<std::string>("dscp", dscp, "_counter");
}

const std::map<int, std::vector<uint8_t>>& kOlympicQueueToDscp() {
  static const std::map<int, std::vector<uint8_t>> queueToDscp = {
      {kOlympicSilverQueueId, {0,  1,  2,  3,  4,  6,  7,  8,  9,  12, 13,
                               14, 15, 40, 41, 42, 43, 44, 45, 46, 47, 49}},

      {kOlympicGoldQueueId, {18, 24, 31, 33, 34, 36, 37, 38, 39}},
      {kOlympicEcn1QueueId, {5}},
      {kOlympicBronzeQueueId, {10, 11, 16, 17, 19, 20, 21, 22, 23, 25, 50, 51,
                               52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63}},
      {kOlympicICPQueueId, {26, 27, 28, 29, 30, 32, 35}},
      {kOlympicNCQueueId, {48}}};
  return queueToDscp;
}

const std::map<int, std::vector<uint8_t>>& kOlympicAllSPQueueToDscp() {
  static const std::map<int, std::vector<uint8_t>> queueToDscp = {
      {kOlympicAllSPNCNFQueueId, {50, 51, 52, 53, 54, 55, 56, 57, 58, 59}},
      {kOlympicAllSPBronzeQueueId,
       {10, 11, 16, 17, 19, 20, 21, 22, 23, 25, 60, 61, 62, 63}},
      {kOlympicAllSPSilverQueueId,
       {0,  1,  2,  3,  4,  6,  7,  8,  9,  12, 13,
        14, 15, 40, 41, 42, 43, 44, 45, 46, 47, 49}},
      {kOlympicAllSPGoldQueueId, {18, 24, 31, 33, 34, 36, 37, 38, 39}},
      {kOlympicAllSPICPQueueId, {26, 27, 28, 29, 30, 32, 35}},
      {kOlympicAllSPNCQueueId, {48}}};
  return queueToDscp;
}

const std::map<int, uint8_t>& kOlympicWRRQueueToWeight() {
  static const std::map<int, uint8_t> wrrQueueToWeight = {
      {kOlympicSilverQueueId, kOlympicSilverWeight},
      {kOlympicGoldQueueId, kOlympicGoldWeight},
      {kOlympicEcn1QueueId, kOlympicEcn1Weight},
      {kOlympicBronzeQueueId, kOlympicBronzeWeight},
  };

  return wrrQueueToWeight;
}

const std::vector<int>& kOlympicWRRQueueIds() {
  static const std::vector<int> wrrQueueIds = {
      kOlympicSilverQueueId,
      kOlympicGoldQueueId,
      kOlympicEcn1QueueId,
      kOlympicBronzeQueueId};

  return wrrQueueIds;
}

const std::vector<int>& kOlympicSPQueueIds() {
  static const std::vector<int> spQueueIds = {
      kOlympicICPQueueId, kOlympicNCQueueId};

  return spQueueIds;
}

const std::vector<int>& kOlympicWRRAndICPQueueIds() {
  static const std::vector<int> wrrAndICPQueueIds = {
      kOlympicSilverQueueId,
      kOlympicGoldQueueId,
      kOlympicEcn1QueueId,
      kOlympicBronzeQueueId,
      kOlympicICPQueueId};
  return wrrAndICPQueueIds;
}

const std::vector<int>& kOlympicWRRAndNCQueueIds() {
  static const std::vector<int> wrrAndNCQueueIds = {
      kOlympicSilverQueueId,
      kOlympicGoldQueueId,
      kOlympicEcn1QueueId,
      kOlympicBronzeQueueId,
      kOlympicNCQueueId};
  return wrrAndNCQueueIds;
}

bool isOlympicWRRQueueId(int queueId) {
  return kOlympicWRRQueueToWeight().find(queueId) !=
      kOlympicWRRQueueToWeight().end();
}

const std::vector<int>& kOlympicAllSPQueueIds() {
  static const std::vector<int> queueIds = {
      kOlympicAllSPNCNFQueueId,
      kOlympicAllSPBronzeQueueId,
      kOlympicAllSPSilverQueueId,
      kOlympicAllSPGoldQueueId,
      kOlympicAllSPICPQueueId,
      kOlympicAllSPNCQueueId};

  return queueIds;
}

void addOlympicQosMapsHelper(
    cfg::SwitchConfig& cfg,
    const std::map<int, std::vector<uint8_t>>& queueToDscpMap) {
  cfg::QosMap qosMap;
  qosMap.dscpMaps_ref()->resize(queueToDscpMap.size());
  ssize_t qosMapIdx = 0;
  for (const auto& q2dscps : queueToDscpMap) {
    auto [q, dscps] = q2dscps;
    *qosMap.dscpMaps_ref()[qosMapIdx].internalTrafficClass_ref() = q;
    for (auto dscp : dscps) {
      qosMap.dscpMaps_ref()[qosMapIdx].fromDscpToTrafficClass_ref()->push_back(
          dscp);
    }
    qosMap.trafficClassToQueueId_ref()->emplace(q, q);
    ++qosMapIdx;
  }
  cfg.qosPolicies_ref()->resize(1);
  *cfg.qosPolicies_ref()[0].name_ref() = "olympic";
  cfg.qosPolicies_ref()[0].qosMap_ref() = qosMap;

  cfg::TrafficPolicyConfig dataPlaneTrafficPolicy;
  dataPlaneTrafficPolicy.defaultQosPolicy_ref() = "olympic";
  cfg.dataPlaneTrafficPolicy_ref() = dataPlaneTrafficPolicy;
  cfg::CPUTrafficPolicyConfig cpuConfig;
  cfg::TrafficPolicyConfig cpuTrafficPolicy;
  cpuTrafficPolicy.defaultQosPolicy_ref() = "olympic";
  cpuConfig.trafficPolicy_ref() = cpuTrafficPolicy;
  cfg.cpuTrafficPolicy_ref() = cpuConfig;
}

void addOlympicQosMaps(cfg::SwitchConfig& cfg) {
  addOlympicQosMapsHelper(cfg, kOlympicQueueToDscp());
}

void addOlympicAllSPQosMaps(cfg::SwitchConfig& cfg) {
  addOlympicQosMapsHelper(cfg, kOlympicAllSPQueueToDscp());
}

int getMaxWeightWRRQueue(const std::map<int, uint8_t>& queueToWeight) {
  auto maxItr = std::max_element(
      queueToWeight.begin(),
      queueToWeight.end(),
      [](const std::pair<int, uint64_t>& p1,
         const std::pair<int, uint64_t>& p2) { return p1.second < p2.second; });

  return maxItr->first;
}

}; // namespace facebook::fboss::utility
