#include "RealmlistCacheSystem.h"
#include <entt.hpp>
#include <Networking/NetStructures.h>
#include "../Components/Singletons/TimeSingleton.h"
#include "../Components/Singletons/DBSingleton.h"
#include "../Components/Singletons/RealmlistCacheSingleton.h"

void RealmlistCacheSystem::Update(entt::registry& registry)
{
    RealmlistCacheSingleton& realmlistCacheSingleton = registry.ctx<RealmlistCacheSingleton>();

    if (realmlistCacheSingleton.refreshTimer >= realmlistCacheSingleton.refreshCacheTime)
    {
        realmlistCacheSingleton.refreshTimer -= realmlistCacheSingleton.refreshCacheTime;

        DBSingleton& dbSingleton = registry.ctx<DBSingleton>();
        if (!dbSingleton.auth.IsOpen())
            return;

        std::shared_ptr<QueryResult> result = dbSingleton.auth.Query("SELECT id, name, type, flags, permissionMask, maxConnections, population FROM realmlist;");
        if (result->GetAffectedRows() == 0)
            return;

        realmlistCacheSingleton.realmlistMap.clear();

        // Build Realmlist Packet
        std::shared_ptr<Bytebuffer>& buffer = realmlistCacheSingleton.realmlistBuffer;
        buffer->Reset();

        buffer->Put(Opcode::SMSG_SEND_REALMLIST);

        size_t realmsNum = realmlistCacheSingleton.realmlistMap.size();
        size_t packetSize = 16 * realmsNum;
        size_t sizeWriteOffset = buffer->writtenData;

        buffer->SkipWrite(sizeof(u16));
        buffer->PutU16(static_cast<u16>(realmsNum));

        while (result->GetNextRow())
        {
            u16 id = result->GetField(0).GetU16();
            std::string name = result->GetField(1).GetString();
            u8 type = result->GetField(2).GetU8();
            u32 flags = result->GetField(3).GetU32();
            u32 permissionMask = result->GetField(4).GetU32();
            i32 maxConnections = result->GetField(5).GetI32();
            u8 population = result->GetField(6).GetU8();

            // Increment size by name + 1 (Strings don't include the null terminator in .size())
            packetSize += name.size() + 1;

            buffer->PutU16(id);
            buffer->PutString(name);
            buffer->PutU8(type);
            buffer->PutU32(flags);
            buffer->PutU32(permissionMask);
            buffer->PutI32(maxConnections);
            buffer->PutU8(population);

            RealmlistEntry realmInfo(id, name, type, flags, permissionMask, maxConnections, population);
            realmlistCacheSingleton.realmlistMap[id] = realmInfo;
        }

        buffer->Put(static_cast<u16>(packetSize), sizeWriteOffset);
    }
    else
    {
        TimeSingleton& timeSingleton = registry.ctx<TimeSingleton>();
        realmlistCacheSingleton.refreshTimer += timeSingleton.deltaTime;
    }
}
