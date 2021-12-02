#pragma once
#include <NovusTypes.h>
#include <robin_hood.h>
#include <mutex>

struct RealmlistEntry
{
    RealmlistEntry() : id(0), name("invalid"), type(0), flags(0), permissionMask(0), maxConnections(0), population(0) { }
    RealmlistEntry(u16 inId, std::string inName, u8 inType, u32 inFlags, u32 inPermissionMask, i32 inMaxConnections, u8 inPopulation) :
        id(inId), name(inName), type(inType), flags(inFlags), permissionMask(inPermissionMask), maxConnections(inMaxConnections), 
        population(inPopulation) { }

    u16 id;
    std::string name;
    u8 type;
    u32 flags;
    u32 permissionMask;
    i32 maxConnections;
    u8 population;
};

struct RealmlistCacheSingleton
{
public:
    RealmlistCacheSingleton() { }
    
    const f32 refreshCacheTime = 30;
    f32 refreshTimer = 30;

    std::shared_ptr<Bytebuffer> realmlistBuffer = Bytebuffer::Borrow<8192>();
    robin_hood::unordered_map<u16, RealmlistEntry> realmlistMap;
};