#pragma once
#include "character.h"
#include <boost/smart_ptr.hpp>

class CPlayer;

struct CPlayerRefCount {
    void* vtable;
    long use_count;
    long weak_count;
};

class CPlayer {
public:
    char pad_0x0000[0x10]; // offset 0x0000

    class CCharacter* m_character; // offset 0x0010
    
    class CPlayer* m_This; // offset 0x0018

    CPlayerRefCount m_RefCount; // offset 0x0020

public:
    

    


};