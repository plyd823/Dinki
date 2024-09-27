#pragma once

namespace NEvent
{
    class CEventImpl
    { };

    struct CEventBase
    {
        unsigned __int16* m_EventImpl;
    };

    template<typename arg0 = void, typename arg1 = void, typename arg2 = void, typename arg3 = void, typename arg4 = void>
    class CSendEvent
    {
    public:
        CEventBase Impl;

        static void SendMsg(const char* a);
        static void SendMsg(uint32_t hash);

        static void SendMsg(const char* event_name, class CGameObject* t1);
    };




    template<typename arg0 = void, typename arg1 = void, typename arg2 = void, typename arg3 = void, typename arg4 = void>
    class CReceiveEvent
    {
        CEventBase Impl;
    };

    class CEventHandler
    {
    public:
        void* m_SubscribedTo;

    public:
        virtual ~CEventHandler() {};
        virtual void HandleEvent(NEvent::CEventImpl const* event, void* data) {};
    };
}