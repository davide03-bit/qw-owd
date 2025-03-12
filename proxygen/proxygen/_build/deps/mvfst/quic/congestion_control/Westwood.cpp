/*
 * This code closely follows the Westwood+ algorithm,
 * adapting it to the QUIC protocol. It maintains the
 * algorithm's core pr inciples of bandwidth estimation
 * and adaptive congestion window management, while
 * integrating with QUIC-specific structures and events.
 */

#include <quic/congestion_control/Westwood.h>
#include <quic/congestion_control/CongestionControlFunctions.h>
#include <quic/logging/QLoggerConstants.h>
#include <chrono>
#include <cmath>
#include <optional>

namespace quic {

    WestwoodRttSampler::WestwoodRttSampler(std::chrono::seconds expiration)
        : expiration_(expiration),
        minRtt_(std::chrono::microseconds::max()),
        rttExpired_(true) {}

    std::chrono::microseconds WestwoodRttSampler::minRtt() const noexcept {
    return minRtt_;
    }

    bool WestwoodRttSampler::minRttExpired() const noexcept {
    return rttExpired_;
    }

    bool WestwoodRttSampler::newRttSample(
        std::chrono::microseconds rttSample,
        std::chrono::steady_clock::time_point sampledTime) noexcept {

    // Determine if previous minRtt is expired by comparing
    // the current sample time with stored timestamp + expiration
    rttExpired_ = minRttTimestamp_.has_value()
        ? (sampledTime > *minRttTimestamp_ + expiration_)
        : false;

    // Update minRtt_ if expired or if we found a smaller RTT
    if (rttExpired_ || rttSample < minRtt_) {
        minRtt_ = rttSample;
        minRttTimestamp_ = sampledTime;
        return true;
    }
    return false;
    }

    void WestwoodRttSampler::resetRttSample(
        std::chrono::steady_clock::time_point sampledTime) noexcept {
    minRtt_ = std::chrono::microseconds::max();
    minRttTimestamp_ = sampledTime;
    rttExpired_ = true;
    }


    constexpr uint64_t kWestwoodMinRttMicroseconds = 50000; // minimum RTT threshold in microseconds
    constexpr uint64_t kWestwoodInitialRttMicroseconds = 20000000; // initial RTT value (20s) before any sample
    constexpr uint16_t kWestwoodRttExpirationSeconds = 20; // expiration time for min RTT in seconds

    Westwood::Westwood(QuicConnectionStateBase &conn)
            :   quicConnectionState_(conn), // reference to the QUIC connection state
                rttWindowStartTime_(Clock::now()), // start RTT measurement interval now
                latestRttSample_(std::chrono::microseconds(kWestwoodInitialRttMicroseconds)), // set initial last RTT
                bandwidthNewestEstimate_(0), // bandwidth newest estimate
                bandwidthEstimate_(0), // smoothed bandwidth estimate
                step_(0),
                bytesAckedInCurrentInterval_(0), // bytes acked during current RTT window
                ssthresh_(std::numeric_limits<uint64_t>::max()), // slow start threshold at max
                rttSampler_(std::chrono::seconds(kWestwoodRttExpirationSeconds)),
                cwndBytes_(conn.transportSettings.initCwndInMss * conn.udpSendPacketLen) {
                cwndBytes_ = boundedCwnd( // congestion window (properly bounded
                    cwndBytes_,
                    quicConnectionState_.udpSendPacketLen,
                    quicConnectionState_.transportSettings.maxCwndInMss,
                    quicConnectionState_.transportSettings.minCwndInMss);
                }
              


    // called when bytes are removed from inflight (unacknowledged) data
    void Westwood::onRemoveBytesFromInflight(uint64_t bytes) {
        // subtract bytes from inflight count, checking for underflow
        subtractAndCheckUnderflow(quicConnectionState_.lossState.inflightBytes, bytes);
        // log the current state for debugging
        VLOG(10) << __func__ << " writable=" << getWritableBytes()
                 << " cwnd=" << cwndBytes_
                 << " inflight=" << quicConnectionState_.lossState.inflightBytes << " " << quicConnectionState_;
        // update congestion metrics in the logger
        if (quicConnectionState_.qLogger) {
            quicConnectionState_.qLogger->addCongestionMetricUpdate(
                    quicConnectionState_.lossState.inflightBytes,
                    getCongestionWindow(),
                    getSlowStartThreshold(),
                    kRemoveInflight);
        }
    }

    // called when a packet is sent
    void Westwood::onPacketSent(const OutstandingPacketWrapper &packet) {
        // add packet size to inflight bytes, checking for overflow
        addAndCheckOverflow(
                quicConnectionState_.lossState.inflightBytes, packet.metadata.encodedSize);
        // LOG
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

    // adapts the congestion window based on ack events
    void Westwood::onAckEvent(const AckEvent &ack) {
        // ensure thge ack object is valid
        DCHECK(ack.largestNewlyAckedPacket.has_value() && !ack.ackedPackets.empty());

        if (ack.rttSample && rttSampler_.newRttSample(ack.rttSample.value(), ack.ackTime)) {
            VLOG(10) << "RTT updated: " << rttSampler_.minRtt().count() << "us";
        }
        if (ack.rttSample) {
            latestRttSample_ = ack.rttSample.value(); // store latest RTT
        }


        // subtract acknowledged bytes from inflight count
        subtractAndCheckUnderflow(quicConnectionState_.lossState.inflightBytes, ack.ackedBytes);
        // LOG
        VLOG(10) << __func__ << " writable=" << getWritableBytes()
                 << " cwnd=" << cwndBytes_
                 << " inflight=" << quicConnectionState_.lossState.inflightBytes << " " << quicConnectionState_;
        if (quicConnectionState_.qLogger) {
            quicConnectionState_.qLogger->addCongestionMetricUpdate(
                    quicConnectionState_.lossState.inflightBytes,
                    getCongestionWindow(),
                    getSlowStartThreshold(),
                    kCongestionPacketAck);
        }

        // process each packet acked
        for (const auto &packet: ack.ackedPackets) {
            onPacketAcked(packet);
        }
        // set the new cwnd based on what came out of the packet processing
        cwndBytes_ = boundedCwnd(
                cwndBytes_,
                quicConnectionState_.udpSendPacketLen,
                quicConnectionState_.transportSettings.maxCwndInMss,
                quicConnectionState_.transportSettings.minCwndInMss);
    }

    // handle individual packet acknowledgements (westwood+ core)
    void Westwood::onPacketAcked(
            const CongestionController::AckEvent::AckPacket &packet) {
        // check if the packet is within the current recovery period
        if (endOfRecovery_ &&
            packet.outstandingPacketMetadata.time < *endOfRecovery_) {
            return; // do not adjust if still in recovery
        }
        // get the size of the acked packet
        uint64_t ackedBytes = packet.outstandingPacketMetadata.encodedSize;
        // update number of acked bytes in the current RTT window
        bytesAckedInCurrentInterval_ += ackedBytes;

        // elapsed time since last BW update
        auto now = Clock::now();    
        //remember that bw will never be estimated if delta never gets bigger than 50ms,
        //that is what happens with very small rtt
        uint64_t delta = std::chrono::duration_cast<std::chrono::microseconds>(now - rttWindowStartTime_).count();
        
        // if elapsed time exceeds max(lastRTT, minimal threshold), recalc bandwidth
        if (delta > std::max((uint64_t)latestRttSample_.count(), kWestwoodMinRttMicroseconds)) {
            step_++;
            updateWestwoodBandwidthEstimates(delta); // update bandwidth estimate
            rttWindowStartTime_ = now; // reset measurement interval start
            bytesAckedInCurrentInterval_ = 0; // reset acked bytes count
            VLOG(1) << "Bw estimate " << bandwidthEstimate_ ;
            VLOG(1) << "CWND bytes  " << cwndBytes_;

        }

        // adjust the congestion window depending on phase
        if (cwndBytes_ < ssthresh_) {
            // in slow start increase cwnd by the number of bytes acked
            addAndCheckOverflow(cwndBytes_, ackedBytes); // slow start: cwnd += ackedBytes
            cwndBytes_ = boundedCwnd(
                cwndBytes_,
                quicConnectionState_.udpSendPacketLen,
                quicConnectionState_.transportSettings.maxCwndInMss,
                quicConnectionState_.transportSettings.minCwndInMss);

        } else {
            // in congestion avoidance increase by the number of bytes acked,
            // but scaled relative to the current cwnd
            uint64_t additionFactor = (kDefaultUDPSendPacketLen *
                                       ackedBytes) /
                                      cwndBytes_;
            addAndCheckOverflow(cwndBytes_, additionFactor); // cwnd += (packet_size * ackedBytes / cwnd)
            cwndBytes_ = boundedCwnd(
                cwndBytes_,
                quicConnectionState_.udpSendPacketLen,
                quicConnectionState_.transportSettings.maxCwndInMss,
                quicConnectionState_.transportSettings.minCwndInMss);
        }
    }

    // calls appropriate functions based on whether it's an ACK or loss event
    void Westwood::onPacketAckOrLoss(
            const AckEvent *FOLLY_NULLABLE ackEvent,
            const LossEvent *FOLLY_NULLABLE lossEvent) {
        if (lossEvent) {
            onPacketLoss(*lossEvent); // handle loss if present
        }
        if (ackEvent && ackEvent->largestNewlyAckedPacket.has_value()) {
            onAckEvent(*ackEvent); // handle ack if present
        }
    }

    // handles losses, calculates ssthresh and adjusts cwnd
    void Westwood::onPacketLoss(const LossEvent &loss) {
        // ensure the loss event is valid
        DCHECK(
                loss.largestLostPacketNum.has_value() &&
                loss.largestLostSentTime.has_value());
        // subtract lost bytes from inflight count
        subtractAndCheckUnderflow(quicConnectionState_.lossState.inflightBytes, loss.lostBytes);

        if (rttSampler_.minRttExpired()) {
            rttSampler_.resetRttSample(Clock::now()); // reset min RTT sample if expired
            VLOG(10) << "RTT expired, resetting RTT sample.";
        }

        // check if this is a new loss event
        if (!endOfRecovery_ || *endOfRecovery_ < *loss.largestLostSentTime) {
            // start a new recovery period
            endOfRecovery_ = Clock::now(); // mark start of new recovery
            // set ssthresh based on estomated bandwidth and minimum rtt
            uint64_t rttMinUs = rttSampler_.minRtt().count(); // current min RTT

        //################################################################################################################
            ssthresh_ = std::max(
                static_cast<uint64_t>((bandwidthEstimate_ * (rttMinUs/1e6))),
                                 2*quicConnectionState_.udpSendPacketLen);
        //################################################################################################################
        
            // set cwnd to current ssthresh
            cwndBytes_ = ssthresh_;
            cwndBytes_ = boundedCwnd(
                cwndBytes_,
                quicConnectionState_.udpSendPacketLen,
                quicConnectionState_.transportSettings.maxCwndInMss,
                quicConnectionState_.transportSettings.minCwndInMss);
            // LOG
            VLOG(10) << __func__ << " exit slow start, ssthresh=" << ssthresh_
                     << " packetNum=" << *loss.largestLostPacketNum
                     << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
                     << " inflight=" << quicConnectionState_.lossState.inflightBytes << " " << quicConnectionState_;
        } else {
            VLOG(10) << __func__ << " packetNum=" << *loss.largestLostPacketNum
                     << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
                     << " inflight=" << quicConnectionState_.lossState.inflightBytes << " " << quicConnectionState_;
        }

        if (quicConnectionState_.qLogger) {
            quicConnectionState_.qLogger->addCongestionMetricUpdate(
                    quicConnectionState_.lossState.inflightBytes,
                    getCongestionWindow(),
                    getSlowStartThreshold(),
                    kCongestionPacketLoss);
        }
        // if the congestion is persistent
        if (loss.persistentCongestion) {
            VLOG(10) << __func__ << " writable=" << getWritableBytes()
                     << " cwnd=" << cwndBytes_
                     << " inflight=" << quicConnectionState_.lossState.inflightBytes << " " << quicConnectionState_;
            if (quicConnectionState_.qLogger) {
                quicConnectionState_.qLogger->addCongestionMetricUpdate(
                        quicConnectionState_.lossState.inflightBytes,
                        getCongestionWindow(),
                        getSlowStartThreshold(),
                        kPersistentCongestion);
            }
            // reset congestion window to minimum value
            cwndBytes_ = quicConnectionState_.transportSettings.minCwndInMss * quicConnectionState_.udpSendPacketLen;
            cwndBytes_ = boundedCwnd(
                cwndBytes_,
                quicConnectionState_.udpSendPacketLen,
                quicConnectionState_.transportSettings.maxCwndInMss,
                quicConnectionState_.transportSettings.minCwndInMss);
        }
    }

    // implements the westwood+ bandwidth estimation filter
    void Westwood::updateWestwoodBandwidthEstimates(uint32_t delta) {
        auto minRtt = rttSampler_.minRtt();
        if (minRtt == std::chrono::microseconds::max()) {
            return; // No valid RTT sample yet, can't update
        }

        // if both bandwidthNewestEstimate_ and bandwidthEstimate_ are 0 (indicating initial state)
        // it simply divides bytesAckedInCurrentInterval_ (bytes acknowledged) by delta to get a raw bandwidth estimate
        // otherwise it uses westwoodLowPassFilter to combine the previous estimate
        // (bandwidthNewestEstimate_) with the new raw estimate (bytesAckedInCurrentInterval_ / delta)
        // TODO: this could lead to loss of precision, especially for small values of delta
        
        // compute a new BW estimate:
        // if no previous estimate, use (bytesAckedInCurrentInterval_/delta)
        // else filter previous estimate with new sample

        //################################################################################################################
        uint64_t bw_ns_est = (bandwidthNewestEstimate_ == 0 && bandwidthEstimate_ == 0)
                            ? bytesAckedInCurrentInterval_ / (delta/1e6)
                            : westwoodLowPassFilter(bandwidthNewestEstimate_, bytesAckedInCurrentInterval_ / (delta/1e6));
        //################################################################################################################

        // update instantaneous bandwidth estimate
        bandwidthNewestEstimate_ = bw_ns_est;

        // update long-term smoothed bandwidth estimate
        bandwidthEstimate_ = westwoodLowPassFilter(bandwidthEstimate_, bw_ns_est);

        // Log the bandwidth estimate
        if (quicConnectionState_.qLogger) {
            quicConnectionState_.qLogger->addBandwidthEstUpdate(
                bandwidthEstimate_, 
                std::chrono::microseconds(delta));
        }
    }

    // implement the low-pass filter: (7/8 * old_value) + (1/8 * new_value)
    // uint32_t Westwood::westwoodLowPassFilter(uint32_t a, uint32_t b) {
    //     return ((7 * a) + b) >> 3;
    // }
    // uint32_t Westwood::westwoodLowPassFilter(uint32_t a, uint32_t b) {
    //     constexpr float T = 10.0; 
    //     float s = static_cast<float>(step_);
    //     float coef = std::tanh(s / T) * (6.0 / 8.0);
    //     float filtered = (coef * static_cast<float>(a)) + ((1.0 - coef ) * static_cast<float>(b));
        
    //     return static_cast<uint32_t>(filtered);
    //     }


    uint32_t Westwood::westwoodLowPassFilter(uint32_t a, uint32_t b) { //20 midpoint 0.5 steepness
        // Parameters to shape the sigmoid:
        constexpr float center = 16.0f;  // Midpoint: sigmoid(center) = 0.5
        constexpr float scale  = 1.0f;   // Controls the steepness of the sigmoid

        float s = static_cast<float>(step_);
        // Compute the sigmoid function:
        float sigmoid = 1.0f / (1.0f + std::exp(-((s - center) / scale)));
        // Optionally scale the coefficient as in the original code:
        float coef = sigmoid * (6.0f / 8.0f);

        float filtered = (coef * static_cast<float>(a)) + ((1.0f - coef) * static_cast<float>(b));
        
        return static_cast<uint32_t>(filtered);
    }


    // calculates the number of bytes that can be sent without exceeding the congestion window
    uint64_t Westwood::getWritableBytes() const noexcept {
        if (quicConnectionState_.lossState.inflightBytes > cwndBytes_) {
            return 0; // no writable bytes if inflight >= cwnd
        } else {
            // TODO: if inflightBytes is somehow larger than cwndBytes, this could cause an underflow.
            uint64_t cwnd = getCongestionWindow();
            subtractAndCheckUnderflow(cwnd, quicConnectionState_.lossState.inflightBytes); // remaining space in cwnd
            return cwnd;
            }
    }

    // returns the current congestion window size
    uint64_t Westwood::getCongestionWindow() const noexcept {
        return cwndBytes_;
    }

    // returns the current slow start threshold
    uint64_t Westwood::getSlowStartThreshold() const noexcept {
        return ssthresh_;
    }

    // determines if the algorithm is currently in slow start phase
    bool Westwood::inSlowStart() const noexcept {
        return cwndBytes_<ssthresh_; // slow start if cwnd < ssthresh
    }

    // returns the type of congestion control algorithm
    CongestionControlType Westwood::type() const noexcept {
        return CongestionControlType::Westwood;
    }

    // returns the current number of bytes in flight
    uint64_t Westwood::getBytesInFlight() const noexcept {
        return quicConnectionState_.lossState.inflightBytes;
    }

    void Westwood::getStats(CongestionControllerStats& stats) const {
        stats.westwoodStats.bw_est = bandwidthEstimate_;
        stats.westwoodStats.rtt_min = rttSampler_.minRtt().count();
        stats.westwoodStats.ssthresh = ssthresh_;
    }

    // placeholders for application-limited behavior, which is not implòemented
    void Westwood::setAppIdle(bool, TimePoint) noexcept { /* unsupported */ }

    void Westwood::setAppLimited() { /* unsupported */ }

    bool Westwood::isAppLimited() const noexcept {
        return false;
    }

} // namespace quic