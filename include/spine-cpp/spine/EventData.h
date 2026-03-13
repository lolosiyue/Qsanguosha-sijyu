#ifndef SPINE_EVENT_DATA_H
#define SPINE_EVENT_DATA_H

#include <spine/SpineString.h>

namespace spine {

class EventData {
public:
    EventData(const String &name) : _name(name), _intValue(0), _floatValue(0) {}
    const String &getName() const { return _name; }

    int getInt() const { return _intValue; }
    void setInt(int v) { _intValue = v; }
    float getFloat() const { return _floatValue; }
    void setFloat(float v) { _floatValue = v; }
    const String &getString() const { return _stringValue; }
    void setString(const String &s) { _stringValue = s; }

private:
    String _name;
    int _intValue;
    float _floatValue;
    String _stringValue;
};

class Event {
public:
    Event(float time, const EventData &data)
        : _time(time), _data(data), _intValue(data.getInt()),
          _floatValue(data.getFloat()), _stringValue(data.getString()) {}

    float getTime() const { return _time; }
    const EventData &getData() const { return _data; }
    int getInt() const { return _intValue; }
    float getFloat() const { return _floatValue; }
    const String &getString() const { return _stringValue; }

private:
    float _time;
    const EventData &_data;
    int _intValue;
    float _floatValue;
    String _stringValue;
};

} // namespace spine

#endif // SPINE_EVENT_DATA_H
