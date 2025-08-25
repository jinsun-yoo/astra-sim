#ifndef EVENT_HH
#define EVENT_HH

#include <string>
#include <stdexcept>
#include <unordered_map>

class ASTRASimGenieNetwork; // Forward declaration

enum EventType {
    UNKNOWN,
    SIM_SEND,
    POLL_SEND,
    SIM_RECV,
    POLL_RECV,
    SCHEDULE_EVENT
};

// Map for converting EventType to string
static const std::unordered_map<EventType, std::string> event_type_to_string_map = {
    {SIM_SEND, "SIM_SEND"},
    {POLL_SEND, "POLL_SEND"},
    {SIM_RECV, "SIM_RECV"},
    {POLL_RECV, "POLL_RECV"},
    {SCHEDULE_EVENT, "SCHEDULE_EVENT"}
};

// Helper function to convert EventType to string
inline std::string event_type_to_string(EventType event_type) {
    auto it = event_type_to_string_map.find(event_type);
    return (it != event_type_to_string_map.end()) ? it->second : "UNKNOWN";
}

class Event {
public:
    Event(
        EventType event_type,
        void *func_arg, 
        const std::string &description = "") : 
        event_type(event_type), func_arg(func_arg), description(description) {}
    
    void trigger_event(ASTRASimGenieNetwork *network);

    EventType get_event_type() const {
        return event_type;
    }

private:
    EventType event_type;
    void *func_arg;
    std::string description;
};

#endif // EVENT_HH
