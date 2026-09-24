#include "ssim/secsgem/gem/identifiers.hpp"

namespace ssim::secsgem::gem {

const std::vector<SvInfo>& status_variables() {
    static const std::vector<SvInfo> table = {
        {kSvidControlState, "ControlState"},
        {kSvidProcessState, "ProcessState"},
        {kSvidCurrentWaferId, "CurrentWaferId"},
        {kSvidCurrentSlot, "CurrentSlot"},
        {kSvidScanProgressPercent, "ScanProgressPercent"},
        {kSvidLastStressMpa, "LastStressMPa"},
        {kSvidLastCurvature, "LastCurvature"},
        {kSvidLastFitRmsUm, "LastFitRmsUm"},
        {kSvidActiveAlarmCount, "ActiveAlarmCount"},
        {kSvidSoftwareRevision, "SoftwareRevision"},
        {kSvidUptimeSeconds, "UptimeSeconds"},
        {kSvidWafersProcessed, "WafersProcessed"},
    };
    return table;
}

const std::vector<std::string>& report_fields(std::uint32_t rptid) {
    static const std::vector<std::string> result = {"WAFER_ID",       "SLOT",      "STRESS_MPA",
                                                    "STRESS_UNC_MPA", "CURVATURE", "FIT_RMS_UM",
                                                    "OUT_OF_SPEC"};
    static const std::vector<std::string> states = {"CONTROL_STATE", "PROCESS_STATE"};
    static const std::vector<std::string> started = {"WAFER_ID", "SLOT", "NUM_LINES"};
    static const std::vector<std::string> none;
    switch (rptid) {
        case kRptidWaferResult:
            return result;
        case kRptidStates:
            return states;
        case kRptidScanStarted:
            return started;
        default:
            return none;
    }
}

std::string ceid_name(std::uint32_t ceid) {
    switch (ceid) {
        case kCeidControlStateChanged:
            return "ControlStateChanged";
        case kCeidProcessStateChanged:
            return "ProcessStateChanged";
        case kCeidCassetteLoaded:
            return "CassetteLoaded";
        case kCeidWaferScanStarted:
            return "WaferScanStarted";
        case kCeidWaferScanComplete:
            return "WaferScanComplete";
        case kCeidWaferOutOfSpec:
            return "WaferOutOfSpec";
        case kCeidCassetteComplete:
            return "CassetteComplete";
        case kCeidRunAborted:
            return "RunAborted";
        default:
            return {};
    }
}

std::uint32_t report_for_event(std::uint32_t ceid) {
    switch (ceid) {
        case kCeidControlStateChanged:
        case kCeidProcessStateChanged:
        case kCeidRunAborted:
            return kRptidStates;
        case kCeidWaferScanStarted:
            return kRptidScanStarted;
        case kCeidWaferScanComplete:
        case kCeidWaferOutOfSpec:
            return kRptidWaferResult;
        default:
            return 0;
    }
}

}  // namespace ssim::secsgem::gem
