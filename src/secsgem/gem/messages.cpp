#include "ssim/secsgem/gem/messages.hpp"

#include <optional>
#include <utility>

namespace ssim::secsgem::gem {

using secs2::Format;
using secs2::Item;
using secs2::Message;

namespace {

using ssim::core::Error;
using ssim::core::Result;
using Body = std::optional<Item>;

Error illegal(const char* what) { return Error{kErrIllegalData, what}; }

Message make(std::uint8_t stream, std::uint8_t function, bool w_bit, Body body) {
    Message m;
    m.stream = stream;
    m.function = function;
    m.w_bit = w_bit;
    m.body = std::move(body);
    return m;
}

Item identity_item(const Identity& id) {
    return Item::list({Item::ascii(id.model), Item::ascii(id.softrev)});
}

// ---- small item predicates ------------------------------------------------

bool is_list(const Item& i, std::size_t n) { return i.is_list() && i.count() == n; }
bool is_ascii(const Item& i) { return i.format() == Format::kAscii; }
bool is_byte(const Item& i) { return i.format() == Format::kBinary && i.count() == 1; }

// Reads every element of an integer item into out. Accepts any integer width
// and rejects negatives, since identifiers are never negative. Returns false
// for a non-integer item.
bool collect_uints(const Item& item, std::vector<std::uint64_t>& out) {
    switch (item.format()) {
        case Format::kU1:
            for (auto v : item.as_array<std::uint8_t>()) out.push_back(v);
            return true;
        case Format::kU2:
            for (auto v : item.as_array<std::uint16_t>()) out.push_back(v);
            return true;
        case Format::kU4:
            for (auto v : item.as_array<std::uint32_t>()) out.push_back(v);
            return true;
        case Format::kU8:
            for (auto v : item.as_array<std::uint64_t>()) out.push_back(v);
            return true;
        case Format::kI1:
            for (auto v : item.as_array<std::int8_t>()) {
                if (v < 0) return false;
                out.push_back(static_cast<std::uint64_t>(v));
            }
            return true;
        case Format::kI2:
            for (auto v : item.as_array<std::int16_t>()) {
                if (v < 0) return false;
                out.push_back(static_cast<std::uint64_t>(v));
            }
            return true;
        case Format::kI4:
            for (auto v : item.as_array<std::int32_t>()) {
                if (v < 0) return false;
                out.push_back(static_cast<std::uint64_t>(v));
            }
            return true;
        case Format::kI8:
            for (auto v : item.as_array<std::int64_t>()) {
                if (v < 0) return false;
                out.push_back(static_cast<std::uint64_t>(v));
            }
            return true;
        default:
            return false;
    }
}

std::optional<std::uint32_t> one_u32(const Item& item) {
    std::vector<std::uint64_t> v;
    if (!collect_uints(item, v) || v.size() != 1 || v[0] > 0xFFFFFFFFull) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(v[0]);
}

// ---- body parsers (shared by the catalogue checks) ------------------------

Result<RemoteCommand> parse_command_body(const Body& body) {
    using R = Result<RemoteCommand>;
    if (!body || !is_list(*body, 2)) return R::err(illegal("S2F41 must be L2"));
    const auto& top = body->as_list();
    if (!is_ascii(top[0])) return R::err(illegal("RCMD must be ASCII"));
    if (!top[1].is_list()) return R::err(illegal("S2F41 parameters must be a list"));
    RemoteCommand command;
    command.rcmd = top[0].as_ascii();
    for (const Item& pair : top[1].as_list()) {
        if (!is_list(pair, 2) || !is_ascii(pair.as_list()[0])) {
            return R::err(illegal("each parameter must be L2{CPNAME A, CPVAL}"));
        }
        command.params.push_back({pair.as_list()[0].as_ascii(), pair.as_list()[1]});
    }
    return R::ok(std::move(command));
}

Result<std::vector<std::uint32_t>> parse_svid_body(const Body& body) {
    using R = Result<std::vector<std::uint32_t>>;
    if (!body || !body->is_list()) return R::err(illegal("S1F3 must be a list of SVIDs"));
    std::vector<std::uint32_t> svids;
    for (const Item& item : body->as_list()) {
        std::vector<std::uint64_t> v;
        if (!collect_uints(item, v)) return R::err(illegal("SVID must be an integer"));
        for (std::uint64_t x : v) {
            if (x > 0xFFFFFFFFull) return R::err(illegal("SVID out of range"));
            svids.push_back(static_cast<std::uint32_t>(x));
        }
    }
    return R::ok(std::move(svids));
}

Result<EventReport> parse_event_body(const Body& body) {
    using R = Result<EventReport>;
    if (!body || !is_list(*body, 3)) return R::err(illegal("S6F11 must be L3"));
    const auto& top = body->as_list();
    auto dataid = one_u32(top[0]);
    auto ceid = one_u32(top[1]);
    if (!dataid || !ceid) return R::err(illegal("DATAID and CEID must be integers"));
    if (!top[2].is_list()) return R::err(illegal("S6F11 reports must be a list"));
    EventReport event;
    event.dataid = *dataid;
    event.ceid = *ceid;
    for (const Item& rpt : top[2].as_list()) {
        if (!is_list(rpt, 2)) return R::err(illegal("each report must be L2{RPTID, values}"));
        auto rptid = one_u32(rpt.as_list()[0]);
        if (!rptid || !rpt.as_list()[1].is_list()) return R::err(illegal("bad report"));
        event.reports.push_back({*rptid, rpt.as_list()[1].as_list()});
    }
    return R::ok(std::move(event));
}

Result<AlarmReport> parse_alarm_body(const Body& body) {
    using R = Result<AlarmReport>;
    if (!body || !is_list(*body, 3)) return R::err(illegal("S5F1 must be L3"));
    const auto& top = body->as_list();
    auto alid = one_u32(top[1]);
    if (!is_byte(top[0]) || !alid || !is_ascii(top[2])) {
        return R::err(illegal("S5F1 must be L3{ALCD B, ALID U4, ALTX A}"));
    }
    return R::ok(AlarmReport{top[0].as_array<std::uint8_t>()[0], *alid, top[2].as_ascii()});
}

Result<CommandAck> parse_command_ack_body(const Body& body) {
    using R = Result<CommandAck>;
    if (!body || !is_list(*body, 2) || !is_byte(body->as_list()[0]) ||
        !body->as_list()[1].is_list()) {
        return R::err(illegal("S2F42 must be L2{HCACK B, list}"));
    }
    CommandAck ack;
    ack.hcack = body->as_list()[0].as_array<std::uint8_t>()[0];
    for (const Item& pair : body->as_list()[1].as_list()) {
        if (!is_list(pair, 2) || !is_ascii(pair.as_list()[0]) || !is_byte(pair.as_list()[1])) {
            return R::err(illegal("each entry must be L2{CPNAME A, CPACK B}"));
        }
        ack.params.push_back(
            {pair.as_list()[0].as_ascii(), pair.as_list()[1].as_array<std::uint8_t>()[0]});
    }
    return R::ok(std::move(ack));
}

Result<std::uint8_t> parse_ack_body(const Body& body) {
    using R = Result<std::uint8_t>;
    if (!body || !is_byte(*body)) return R::err(illegal("acknowledge must be one binary byte"));
    return R::ok(body->as_array<std::uint8_t>()[0]);
}

bool no_body(const Body& b) { return !b.has_value(); }

// S1F13 from a host: nothing, an empty list, or the equipment-style L2{A,A}.
bool comm_request_ok(const Body& b) {
    if (!b) return true;
    return is_list(*b, 0) ||
           (is_list(*b, 2) && is_ascii(b->as_list()[0]) && is_ascii(b->as_list()[1]));
}

bool identity_ok(const Item& i) {
    return is_list(i, 2) && is_ascii(i.as_list()[0]) && is_ascii(i.as_list()[1]);
}

bool s1f14_ok(const Body& b) {
    return b && is_list(*b, 2) && is_byte(b->as_list()[0]) &&
           (is_list(b->as_list()[1], 0) || identity_ok(b->as_list()[1]));
}

bool s9_ok(const Body& b) { return b && b->format() == Format::kBinary && b->count() == 10; }

}  // namespace

// ---- builders --------------------------------------------------------------

Message make_s1f1() { return make(1, 1, true, std::nullopt); }
Message make_s1f2(const Identity& id) { return make(1, 2, false, identity_item(id)); }
Message make_s1f13() { return make(1, 13, true, Item::list({})); }
Message make_s1f14(std::uint8_t commack, const Identity& id) {
    return make(1, 14, false, Item::list({Item::binary({commack}), identity_item(id)}));
}
Message make_s1f3(const std::vector<std::uint32_t>& svids) {
    Item::List items;
    for (std::uint32_t v : svids) items.push_back(Item::u4(v));
    return make(1, 3, true, Item::list(std::move(items)));
}
Message make_s1f4(std::vector<Item> values) {
    return make(1, 4, false, Item::list(std::move(values)));
}
Message make_s1f15() { return make(1, 15, true, std::nullopt); }
Message make_s1f16(std::uint8_t oflack) { return make(1, 16, false, Item::binary({oflack})); }
Message make_s1f17() { return make(1, 17, true, std::nullopt); }
Message make_s1f18(std::uint8_t onlack) { return make(1, 18, false, Item::binary({onlack})); }

Message make_s2f41(const RemoteCommand& command) {
    Item::List params;
    for (const CommandParam& p : command.params) {
        params.push_back(Item::list({Item::ascii(p.name), p.value}));
    }
    return make(2, 41, true,
                Item::list({Item::ascii(command.rcmd), Item::list(std::move(params))}));
}
Message make_s2f42(std::uint8_t hcack, const std::vector<ParamAck>& params) {
    Item::List acks;
    for (const ParamAck& p : params) {
        acks.push_back(Item::list({Item::ascii(p.name), Item::binary({p.cpack})}));
    }
    return make(2, 42, false, Item::list({Item::binary({hcack}), Item::list(std::move(acks))}));
}

Message make_s5f1(const AlarmReport& alarm) {
    return make(
        5, 1, true,
        Item::list({Item::binary({alarm.alcd}), Item::u4(alarm.alid), Item::ascii(alarm.altx)}));
}
Message make_s5f2(std::uint8_t ackc5) { return make(5, 2, false, Item::binary({ackc5})); }

Message make_s6f11(const EventReport& event) {
    Item::List reports;
    for (const Report& r : event.reports) {
        reports.push_back(Item::list({Item::u4(r.rptid), Item::list(r.values)}));
    }
    return make(
        6, 11, true,
        Item::list({Item::u4(event.dataid), Item::u4(event.ceid), Item::list(std::move(reports))}));
}
Message make_s6f12(std::uint8_t ackc6) { return make(6, 12, false, Item::binary({ackc6})); }

Message make_s9(std::uint8_t function, const std::vector<std::uint8_t>& header_bytes) {
    return make(9, function, false, Item::binary(header_bytes));
}

// ---- parsers ---------------------------------------------------------------

Result<RemoteCommand> parse_s2f41(const Message& m) { return parse_command_body(m.body); }
Result<CommandAck> parse_s2f42(const Message& m) { return parse_command_ack_body(m.body); }
Result<std::vector<std::uint32_t>> parse_s1f3(const Message& m) { return parse_svid_body(m.body); }
Result<EventReport> parse_s6f11(const Message& m) { return parse_event_body(m.body); }
Result<AlarmReport> parse_s5f1(const Message& m) { return parse_alarm_body(m.body); }
Result<std::uint8_t> parse_ack(const Message& m) { return parse_ack_body(m.body); }

// ---- catalogues ------------------------------------------------------------

void register_equipment_catalogue(secs2::MessageFactory& f) {
    f.register_message(1, 1, "S1F1", no_body);
    f.register_message(1, 3, "S1F3", [](const Body& b) { return parse_svid_body(b).has_value(); });
    f.register_message(1, 13, "S1F13", comm_request_ok);
    f.register_message(1, 15, "S1F15", no_body);
    f.register_message(1, 17, "S1F17", no_body);
    f.register_message(2, 41, "S2F41",
                       [](const Body& b) { return parse_command_body(b).has_value(); });
    f.register_message(5, 2, "S5F2", [](const Body& b) { return parse_ack_body(b).has_value(); });
    f.register_message(6, 12, "S6F12", [](const Body& b) { return parse_ack_body(b).has_value(); });
}

void register_host_catalogue(secs2::MessageFactory& f) {
    f.register_message(1, 2, "S1F2", [](const Body& b) { return b && identity_ok(*b); });
    f.register_message(1, 4, "S1F4", [](const Body& b) { return b && b->is_list(); });
    f.register_message(1, 14, "S1F14", s1f14_ok);
    f.register_message(1, 16, "S1F16", [](const Body& b) { return parse_ack_body(b).has_value(); });
    f.register_message(1, 18, "S1F18", [](const Body& b) { return parse_ack_body(b).has_value(); });
    f.register_message(2, 42, "S2F42",
                       [](const Body& b) { return parse_command_ack_body(b).has_value(); });
    f.register_message(5, 1, "S5F1", [](const Body& b) { return parse_alarm_body(b).has_value(); });
    f.register_message(6, 11, "S6F11",
                       [](const Body& b) { return parse_event_body(b).has_value(); });
    for (std::uint8_t fn : {1, 3, 5, 7, 9, 11}) {
        f.register_message(9, fn, "S9F" + std::to_string(fn), s9_ok);
    }
}

}  // namespace ssim::secsgem::gem
