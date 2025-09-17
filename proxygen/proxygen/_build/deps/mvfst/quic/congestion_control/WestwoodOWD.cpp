/*
 * This code closely follows the Westwood+ algorithm,
 * adapting it to the QUIC protocol. It maintains the
 * algorithm's core principles of bandwidth estimation
 * and adaptive congestion window management, while
 * integrating with QUIC-specific structures and events.
 * This version specifically implements Delay Control
 * based on QUIC Westwood+.
 */

#include <folly/MapUtil.h>
#include <quic/congestion_control/CongestionControlFunctions.h>
#include <quic/congestion_control/WestwoodOWD.h>
#include <quic/logging/QLoggerConstants.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <algorithm> // std::sort()

namespace quic {

WestwoodOWDRttSampler::WestwoodOWDRttSampler(std::chrono::seconds expiration)
    : expiration_(expiration),
      minRtt_(std::chrono::microseconds::max()),
      maxRttSinceLastLoss_(std::chrono::microseconds(0)),
      rttExpired_(true) {}

std::chrono::microseconds WestwoodOWDRttSampler::minRtt() const noexcept {
  return minRtt_;
}

// Retrieve the current max RTT and reset it for the next epoch.
std::chrono::microseconds WestwoodOWDRttSampler::maxRtt() noexcept {
  auto peak = maxRttSinceLastLoss_;
  maxRttSinceLastLoss_ = std::chrono::microseconds(0);
  return peak;
}

bool WestwoodOWDRttSampler::minRttExpired() const noexcept {
  return rttExpired_;
}

bool WestwoodOWDRttSampler::newRttSample(
    std::chrono::microseconds rttSample,
    std::chrono::steady_clock::time_point sampledTime) noexcept {
  // Check if the previous min RTT is expired.
  rttExpired_ = minRttTimestamp_.has_value()
      ? (sampledTime > *minRttTimestamp_ + expiration_)
      : false;

  // Update minRtt_ if expired or if a smaller RTT is found.
  if (rttExpired_ || rttSample < minRtt_) {
    minRtt_ = rttSample;
    minRttTimestamp_ = sampledTime;
  }

  // Track the largest RTT seen since the last loss.
  if (rttSample > maxRttSinceLastLoss_) {
    maxRttSinceLastLoss_ = rttSample;
  }

  return true;
}

void WestwoodOWDRttSampler::resetRttSample(
    std::chrono::steady_clock::time_point sampledTime) noexcept {
  minRtt_ = std::chrono::microseconds::max();
  minRttTimestamp_ = sampledTime;
  rttExpired_ = true;
}

// ==================== WestwoodOWD ====================

constexpr uint64_t kWestwoodOWDMinRttMicroseconds = 50000;
constexpr uint64_t kWestwoodOWDInitialRttMicroseconds = 20000000;
constexpr uint16_t kWestwoodOWDRttExpirationSeconds = 20;
constexpr double alphaBias = 0.999;

WestwoodOWD::WestwoodOWD(QuicConnectionStateBase& conn)
    : quicConnectionState_(conn),
      rttWindowStartTime_(Clock::now()),
      latestRttSample_(
          std::chrono::microseconds(kWestwoodOWDInitialRttMicroseconds)),
      bandwidthNewestEstimate_(0),
      bandwidthEstimate_(0),
      bytesAckedInCurrentInterval_(0),
      ssthresh_(std::numeric_limits<uint64_t>::max()),
      rttSampler_(std::chrono::seconds(kWestwoodOWDRttExpirationSeconds)),
      cwndBytes_(conn.transportSettings.initCwndInMss * conn.udpSendPacketLen),
      latestSendTimeStamp_(std::chrono::steady_clock::time_point::min()),
      latestReceiveTimeStamp_(0),
      interDeparture_(0),
      interArrival_(0),
      owdv_(0),
      owdvCorrect_(0),
      owd_(0),
      biasEstimation_(0),
      OneWayDelayMax_(0),
      OneWayDelayWindowStartTime_(Clock::now()) {
  cwndBytes_ = boundedCwnd(
      cwndBytes_,
      quicConnectionState_.udpSendPacketLen,
      quicConnectionState_.transportSettings.maxCwndInMss,
      quicConnectionState_.transportSettings.minCwndInMss);
}

void WestwoodOWD::onRemoveBytesFromInflight(uint64_t bytes) {
  subtractAndCheckUnderflow(
      quicConnectionState_.lossState.inflightBytes, bytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_
           << " inflight=" << quicConnectionState_.lossState.inflightBytes
           << " " << quicConnectionState_;
  if (quicConnectionState_.qLogger) {
    quicConnectionState_.qLogger->addCongestionMetricUpdate(
        quicConnectionState_.lossState.inflightBytes,
        getCongestionWindow(),
        getSlowStartThreshold(),
        kRemoveInflight);
  }
}

void WestwoodOWD::onPacketSent(const OutstandingPacketWrapper& packet) {
  addAndCheckOverflow(
      quicConnectionState_.lossState.inflightBytes,
      packet.metadata.encodedSize);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_
           << " inflight=" << quicConnectionState_.lossState.inflightBytes
           << " packetNum=" << packet.packet.header.getPacketSequenceNum()
           << " " << quicConnectionState_;
  if (quicConnectionState_.qLogger) {
    quicConnectionState_.qLogger->addCongestionMetricUpdate(
        quicConnectionState_.lossState.inflightBytes,
        getCongestionWindow(),
        getSlowStartThreshold(),
        kCongestionPacketSent);
  }
}

void WestwoodOWD::onAckEvent(const AckEvent& ack) {
  DCHECK(ack.largestNewlyAckedPacket.has_value() && !ack.ackedPackets.empty());

  if (ack.rttSample &&
      rttSampler_.newRttSample(ack.rttSample.value(), ack.ackTime)) {
    VLOG(10) << "RTT updated: " << rttSampler_.minRtt().count() << "us";
  }
  if (ack.rttSample) {
    latestRttSample_ = ack.rttSample.value();
  }

  subtractAndCheckUnderflow(
      quicConnectionState_.lossState.inflightBytes, ack.ackedBytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_
           << " inflight=" << quicConnectionState_.lossState.inflightBytes
           << " " << quicConnectionState_;
  if (quicConnectionState_.qLogger) {
    quicConnectionState_.qLogger->addCongestionMetricUpdate(
        quicConnectionState_.lossState.inflightBytes,
        getCongestionWindow(),
        getSlowStartThreshold(),
        kCongestionPacketAck);
  }

  for (const auto& packet : ack.ackedPackets) {
    updateOneWayDelay(packet);
    onPacketAcked(packet);
  }

  cwndBytes_ = boundedCwnd(
      cwndBytes_,
      quicConnectionState_.udpSendPacketLen,
      quicConnectionState_.transportSettings.maxCwndInMss,
      quicConnectionState_.transportSettings.minCwndInMss);
}

// bool WestwoodOWD::delayControl(double delayThresholdFraction) {
//   uint64_t rttMinUs = rttSampler_.minRtt().count();

//   if (lossMaxRtt_.count() == 0)
//     return false;
//   if (static_cast<uint64_t>(lossMaxRtt_.count()) > rttMinUs &&
//       (owd_ > (delayThresholdFraction * (lossMaxRtt_.count() - rttMinUs)))) {
//     return true;
//   }
//   return false;
// }

void WestwoodOWD::updateOneWayDelay(
    const CongestionController::AckEvent::AckPacket& packet) {
  if (!packet.receiveRelativeTimeStampUsec.has_value()) {
    return;
  }
  if (isFirstPacket()) {
    latestReceiveTimeStamp_ =
        packet.receiveRelativeTimeStampUsec.value().count();
    latestSendTimeStamp_ = packet.outstandingPacketMetadata.time;
    return;
  }
  auto currentSendTimeStamp = packet.outstandingPacketMetadata.time;
  interDeparture_ = std::chrono::duration_cast<std::chrono::microseconds>(
                        currentSendTimeStamp - latestSendTimeStamp_)
                        .count();

  auto currentReceiveTimeStamp =
      packet.receiveRelativeTimeStampUsec.value().count();
  interArrival_ = currentReceiveTimeStamp - latestReceiveTimeStamp_;

  latestSendTimeStamp_ = currentSendTimeStamp;
  latestReceiveTimeStamp_ = currentReceiveTimeStamp;

  auto timestamp_owd = Clock::now();
  auto time_owd = timestamp_owd.time_since_epoch();
  auto time_owd_us =
      std::chrono::duration_cast<std::chrono::microseconds>(time_owd).count();

  owdv_ = interArrival_ - interDeparture_;

  biasEstimation_ = alphaBias * biasEstimation_ + (1 - alphaBias) * owdv_;

  owdvCorrect_ = owdv_ - biasEstimation_;

  owd_ += owdvCorrect_;

  // Clamp owd in order to reject nonsense queue negative levels

  owd_ = std::max(static_cast<int64_t>(0), owd_);

  auto now = Clock::now();
  uint64_t delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - OneWayDelayWindowStartTime_).count();

  if (delta < 30)
    OneWayDelayVec_.push_back(owd_);
    std::cout << "yuppi";
  else {
    std::sort(OneWayDelayVec_.begin(), OneWayDelayVec_.end());
    OneWayDelayMax_ = OneWayDelayVec_[static_cast<size_t>(0.95 * OneWayDelayVec_.size())];
    OneWayDelayVec_.clear();
    OneWayDelayWindowStartTime_ = now;
  }

  std::cout << time_owd_us << " " << owd_ << " " << owdvCorrect_ << " "
            << OneWayDelayMax_ << " " << std::endl;
}

void WestwoodOWD::onPacketAcked(
    const CongestionController::AckEvent::AckPacket& packet) {
  if (endOfRecovery_ &&
      packet.outstandingPacketMetadata.time < *endOfRecovery_) {
    return;
  }
  uint64_t ackedBytes = packet.outstandingPacketMetadata.encodedSize;
  bytesAckedInCurrentInterval_ += ackedBytes;

  auto now = Clock::now();
  uint64_t delta = std::chrono::duration_cast<std::chrono::microseconds>(
                       now - rttWindowStartTime_)
                       .count();

  if (delta >
      std::max(
          (uint64_t)latestRttSample_.count(), kWestwoodOWDMinRttMicroseconds)) {
    updateWestwoodBandwidthEstimates(delta);
    rttWindowStartTime_ = now;
    bytesAckedInCurrentInterval_ = 0;
    VLOG(1) << "Bw estimate " << bandwidthEstimate_;
    VLOG(1) << "CWND bytes  " << cwndBytes_;
  }

  // If the delay condition is met, adjust ssthresh and cwnd.
  // if (delayControl(quicConnectionState_.transportSettings.ccaConfig
  //                     .delayControlFraction)) {
  //   uint64_t rttMinUs = rttSampler_.minRtt().count();
  //   ssthresh_ = std::max(
  //       static_cast<uint64_t>(bandwidthEstimate_ * (rttMinUs / 1e6)),
  //       2 * quicConnectionState_.udpSendPacketLen);
  //   cwndBytes_ = ssthresh_;
  //   cwndBytes_ = boundedCwnd(
  //       cwndBytes_,
  //       quicConnectionState_.udpSendPacketLen,
  //       quicConnectionState_.transportSettings.maxCwndInMss,
  //       quicConnectionState_.transportSettings.minCwndInMss);
  // }

  // VLOG(10) << __func__ << " delay control triggered, ssthresh=" << ssthresh_
  //          << " ackedBytes=" << ackedBytes << " writable=" << getWritableBytes()
  //          << " cwnd=" << cwndBytes_
  //          << " inflight=" << quicConnectionState_.lossState.inflightBytes
  //          << " " << quicConnectionState_;

  // if (quicConnectionState_.qLogger) {
  //   quicConnectionState_.qLogger->addCongestionMetricUpdate(
  //       quicConnectionState_.lossState.inflightBytes,
  //       getCongestionWindow(),
  //       getSlowStartThreshold(),
  //       kCongestionDelaySignal);
  // }

  // Slow start or congestion avoidance increment:
  if (cwndBytes_ < ssthresh_) {
    // Slow start: add ackedBytes directly
    addAndCheckOverflow(cwndBytes_, ackedBytes);
  } else {
    // Congestion avoidance: add fraction of cwnd
    uint64_t additionFactor =
        (kDefaultUDPSendPacketLen * ackedBytes) / cwndBytes_;
    addAndCheckOverflow(cwndBytes_, additionFactor);
  }

  cwndBytes_ = boundedCwnd(
      cwndBytes_,
      quicConnectionState_.udpSendPacketLen,
      quicConnectionState_.transportSettings.maxCwndInMss,
      quicConnectionState_.transportSettings.minCwndInMss);
}

void WestwoodOWD::onPacketAckOrLoss(
    const AckEvent* FOLLY_NULLABLE ackEvent,
    const LossEvent* FOLLY_NULLABLE lossEvent) {
  if (lossEvent) {
    onPacketLoss(*lossEvent);
  }
  if (ackEvent && ackEvent->largestNewlyAckedPacket.has_value()) {
    onAckEvent(*ackEvent);
  }
}

void WestwoodOWD::onPacketLoss(const LossEvent& loss) {
  DCHECK(
      loss.largestLostPacketNum.has_value() &&
      loss.largestLostSentTime.has_value());
  subtractAndCheckUnderflow(
      quicConnectionState_.lossState.inflightBytes, loss.lostBytes);

  uint64_t rttMinUs = rttSampler_.minRtt().count();

  if (rttSampler_.minRttExpired()) {
    rttSampler_.resetRttSample(Clock::now());
    VLOG(10) << "RTT expired, resetting RTT sample.";
  }

  if (!endOfRecovery_ || *endOfRecovery_ < *loss.largestLostSentTime) {
    endOfRecovery_ = Clock::now();
    uint64_t rttMinUs = rttSampler_.minRtt().count();
    ssthresh_ = std::max(
        static_cast<uint64_t>(bandwidthEstimate_ * (rttMinUs / 1e6)),
        2 * quicConnectionState_.udpSendPacketLen);
    cwndBytes_ = ssthresh_;
    cwndBytes_ = boundedCwnd(
        cwndBytes_,
        quicConnectionState_.udpSendPacketLen,
        quicConnectionState_.transportSettings.maxCwndInMss,
        quicConnectionState_.transportSettings.minCwndInMss);

    VLOG(10) << __func__ << " exit slow start, ssthresh=" << ssthresh_
             << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << quicConnectionState_.lossState.inflightBytes
             << " " << quicConnectionState_;
  } else {
    VLOG(10) << __func__ << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << quicConnectionState_.lossState.inflightBytes
             << " " << quicConnectionState_;
  }

  if (quicConnectionState_.qLogger) {
    quicConnectionState_.qLogger->addCongestionMetricUpdate(
        quicConnectionState_.lossState.inflightBytes,
        getCongestionWindow(),
        getSlowStartThreshold(),
        kCongestionPacketLoss);
  }
  if (loss.persistentCongestion) {
    VLOG(10) << __func__ << " writable=" << getWritableBytes()
             << " cwnd=" << cwndBytes_
             << " inflight=" << quicConnectionState_.lossState.inflightBytes
             << " " << quicConnectionState_;
    if (quicConnectionState_.qLogger) {
      quicConnectionState_.qLogger->addCongestionMetricUpdate(
          quicConnectionState_.lossState.inflightBytes,
          getCongestionWindow(),
          getSlowStartThreshold(),
          kPersistentCongestion);
    }
    cwndBytes_ = quicConnectionState_.transportSettings.minCwndInMss *
        quicConnectionState_.udpSendPacketLen;
    cwndBytes_ = boundedCwnd(
        cwndBytes_,
        quicConnectionState_.udpSendPacketLen,
        quicConnectionState_.transportSettings.maxCwndInMss,
        quicConnectionState_.transportSettings.minCwndInMss);
  }
}

void WestwoodOWD::updateWestwoodBandwidthEstimates(uint32_t delta) {
  auto minRtt = rttSampler_.minRtt();
  if (minRtt == std::chrono::microseconds::max()) {
    return;
  }
  uint64_t bw_ns_est =
      (bandwidthNewestEstimate_ == 0 && bandwidthEstimate_ == 0)
      ? bytesAckedInCurrentInterval_ / (delta / 1e6)
      : westwoodLowPassFilter(
            bandwidthNewestEstimate_,
            bytesAckedInCurrentInterval_ / (delta / 1e6));
  bandwidthNewestEstimate_ = bw_ns_est;
  bandwidthEstimate_ = westwoodLowPassFilter(bandwidthEstimate_, bw_ns_est);

  if (quicConnectionState_.qLogger) {
    quicConnectionState_.qLogger->addBandwidthEstUpdate(
        bandwidthEstimate_, std::chrono::microseconds(delta));
  }
}

uint32_t WestwoodOWD::westwoodLowPassFilter(uint32_t a, uint32_t b) {
  float coef = 2.0f / 8.0f;
  float filtered =
      (coef * static_cast<float>(a)) + ((1.0f - coef) * static_cast<float>(b));
  return static_cast<uint32_t>(filtered);
}

uint64_t WestwoodOWD::getWritableBytes() const noexcept {
  if (quicConnectionState_.lossState.inflightBytes > cwndBytes_) {
    return 0;
  } else {
    uint64_t cwnd = getCongestionWindow();
    subtractAndCheckUnderflow(
        cwnd, quicConnectionState_.lossState.inflightBytes);
    return cwnd;
  }
}

bool WestwoodOWD::isFirstPacket() {
  return latestSendTimeStamp_ == std::chrono::steady_clock::time_point::min();
}

uint64_t WestwoodOWD::getCongestionWindow() const noexcept {
  return cwndBytes_;
}

uint64_t WestwoodOWD::getSlowStartThreshold() const noexcept {
  return ssthresh_;
}

uint64_t WestwoodOWD::getOneWayDelay() const noexcept {
  return owd_;
}

uint64_t WestwoodOWD::getOneWayDelayVariation() const noexcept {
  return owdv_;
}

bool WestwoodOWD::inSlowStart() const noexcept {
  return cwndBytes_ < ssthresh_;
}

CongestionControlType WestwoodOWD::type() const noexcept {
  return CongestionControlType::WestwoodOWD;
}

uint64_t WestwoodOWD::getBytesInFlight() const noexcept {
  return quicConnectionState_.lossState.inflightBytes;
}

void WestwoodOWD::getStats(CongestionControllerStats& stats) const {
  stats.westwoodOWDStats.bw_est = bandwidthEstimate_;
  stats.westwoodOWDStats.rtt_min = rttSampler_.minRtt().count();
  stats.westwoodOWDStats.ssthresh = ssthresh_;
  stats.westwoodOWDStats.owd = owd_;
  stats.westwoodOWDStats.owdv = owdv_;
}

void WestwoodOWD::setAppIdle(bool, TimePoint) noexcept { /* unsupported */ }
void WestwoodOWD::setAppLimited() { /* unsupported */ }
bool WestwoodOWD::isAppLimited() const noexcept {
  return false;
}

} // namespace quic
