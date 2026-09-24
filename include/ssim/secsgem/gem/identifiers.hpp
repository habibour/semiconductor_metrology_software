#pragma once

// Thread-safety: constants and pure lookups only.
//
// CLAUDE.md 5.2: every project-defined identifier lives in this one file.
// CEID, RPTID, ALID, SVID and ECID numbers belong to this project, not to any
// standard (PRD 8.6). Names and layouts follow PRD 8.6.5 and 8.6.6.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ssim::secsgem::gem {

// ---- collection events (CEID) --------------------------------------------
constexpr std::uint32_t kCeidControlStateChanged = 2001;
constexpr std::uint32_t kCeidProcessStateChanged = 2002;
constexpr std::uint32_t kCeidCassetteLoaded = 2003;  // cassette loop not built yet
constexpr std::uint32_t kCeidWaferScanStarted = 2004;
constexpr std::uint32_t kCeidWaferScanComplete = 2005;
constexpr std::uint32_t kCeidWaferOutOfSpec = 2006;
constexpr std::uint32_t kCeidCassetteComplete = 2007;  // cassette loop not built yet
constexpr std::uint32_t kCeidRunAborted = 2008;

// ---- reports (RPTID) -------------------------------------------------------
// 3001: WAFER_ID A, SLOT U1, STRESS_MPA F4, STRESS_UNC_MPA F4, CURVATURE F4,
//       FIT_RMS_UM F4, OUT_OF_SPEC BOOLEAN
// 3002: CONTROL_STATE U1, PROCESS_STATE U1
// 3003: WAFER_ID A, SLOT U1, NUM_LINES U1
constexpr std::uint32_t kRptidWaferResult = 3001;
constexpr std::uint32_t kRptidStates = 3002;
constexpr std::uint32_t kRptidScanStarted = 3003;

// ---- alarms (ALID); the same numbers as ssim::core::AlarmId ---------------
constexpr std::uint32_t kAlidInternalError = 1008;  // not raised through the alarm manager

// ---- status variables (SVID) ----------------------------------------------
constexpr std::uint32_t kSvidControlState = 4001;
constexpr std::uint32_t kSvidProcessState = 4002;
constexpr std::uint32_t kSvidCurrentWaferId = 4003;
constexpr std::uint32_t kSvidCurrentSlot = 4004;
constexpr std::uint32_t kSvidScanProgressPercent = 4005;
constexpr std::uint32_t kSvidLastStressMpa = 4006;
constexpr std::uint32_t kSvidLastCurvature = 4007;
constexpr std::uint32_t kSvidLastFitRmsUm = 4008;
constexpr std::uint32_t kSvidActiveAlarmCount = 4009;
constexpr std::uint32_t kSvidSoftwareRevision = 4010;
constexpr std::uint32_t kSvidUptimeSeconds = 4011;
constexpr std::uint32_t kSvidWafersProcessed = 4012;

// ---- equipment constants (ECID); defined here, served by FR-GEM-7 later ----
constexpr std::uint32_t kEcidEdgeExclusionMm = 5001;
constexpr std::uint32_t kEcidNumScanLines = 5002;
constexpr std::uint32_t kEcidStressSpecLowMpa = 5003;
constexpr std::uint32_t kEcidStressSpecHighMpa = 5004;
constexpr std::uint32_t kEcidFitRmsLimitUm = 5005;
constexpr std::uint32_t kEcidPointsPerMm = 5006;

// ---- acknowledge codes -----------------------------------------------------
constexpr std::uint8_t kCommackAccepted = 0;
constexpr std::uint8_t kCommackDenied = 1;

constexpr std::uint8_t kOflackOk = 0;

constexpr std::uint8_t kOnlackAccepted = 0;
constexpr std::uint8_t kOnlackNotAllowed = 1;
constexpr std::uint8_t kOnlackAlreadyOnline = 2;

// HCACK (PRD 8.6.3): 0 done, 1 unknown command, 2 cannot perform now,
// 3 invalid parameter, 4 accepted and completion signalled later by an event.
constexpr std::uint8_t kHcackDone = 0;
constexpr std::uint8_t kHcackUnknownCommand = 1;
constexpr std::uint8_t kHcackCannotPerformNow = 2;
constexpr std::uint8_t kHcackInvalidParameter = 3;
constexpr std::uint8_t kHcackAcceptedLater = 4;

// CPACK per parameter. TODO(verify): values from memory of public descriptions.
constexpr std::uint8_t kCpackOk = 0;
constexpr std::uint8_t kCpackUnknownName = 1;
constexpr std::uint8_t kCpackIllegalValue = 2;
constexpr std::uint8_t kCpackIllegalFormat = 3;

constexpr std::uint8_t kAckAccepted = 0;  // ACKC5 / ACKC6

// ALCD: top bit set = alarm set. The category in the low bits is project
// defined. TODO(verify) against the standard's category list.
constexpr std::uint8_t kAlcdSetBit = 0x80;
constexpr std::uint8_t kAlarmCategory = 0x05;

// ---- lookups ---------------------------------------------------------------

struct SvInfo {
    std::uint32_t svid;
    const char* name;
};

// All status variables in SVID order (an empty S1F3 asks for these).
const std::vector<SvInfo>& status_variables();

// Report layout: field names in order. Empty for an unknown RPTID.
const std::vector<std::string>& report_fields(std::uint32_t rptid);

// "WaferScanComplete", ... or an empty string for an unknown CEID.
std::string ceid_name(std::uint32_t ceid);

// The report a CEID carries, or 0 if the event is not defined.
std::uint32_t report_for_event(std::uint32_t ceid);

}  // namespace ssim::secsgem::gem
