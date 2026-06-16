#include "EventBus.h"

EventBus::Sub EventBus::_subs[EventBus::MAX_SUBS] = {};
int EventBus::_n = 0;

bool EventBus::subscribe(Handler h, void* ctx)
{
    if (!h || _n >= MAX_SUBS) return false;
    _subs[_n].h = h;
    _subs[_n].ctx = ctx;
    _n++;
    return true;
}

void EventBus::publish(Event e)
{
    for (int i = 0; i < _n; i++)
        _subs[i].h(e, _subs[i].ctx);
}

int EventBus::subscriberCount() { return _n; }

void EventBus::reset() { _n = 0; }
