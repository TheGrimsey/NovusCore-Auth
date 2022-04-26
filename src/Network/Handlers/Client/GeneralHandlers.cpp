#include "GeneralHandlers.h"
#include <Networking/NetStructures.h>
#include <Networking/NetPacket.h>
#include <Networking/NetClient.h>
#include <Networking/NetPacketHandler.h>
#include <Networking/PacketUtils.h>

#include "../../../Utils/ServiceLocator.h"
#include "../../../ECS/Components/Singletons/RealmlistCacheSingleton.h"
#include "../../../ECS/Components/Network/ConnectionSingleton.h"

namespace Client
{
    void GeneralHandlers::Setup(NetPacketHandler* netPacketHandler)
    {
        netPacketHandler->SetMessageHandler(Opcode::CMSG_REQUEST_REALMLIST, { ConnectionStatus::CONNECTED, 0, GeneralHandlers::HandleRequestRealmlist });
        netPacketHandler->SetMessageHandler(Opcode::CMSG_SELECT_REALM, { ConnectionStatus::CONNECTED, sizeof(u16), GeneralHandlers::HandleSelectRealm});
    }

    bool GeneralHandlers::HandleRequestRealmlist(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetRegistry();
        RealmlistCacheSingleton& realmlistCacheSingleton = registry->ctx<RealmlistCacheSingleton>();

        // Send Realmlist
        netClient->Send(realmlistCacheSingleton.realmlistBuffer);

        return true;
    }
    bool GeneralHandlers::HandleSelectRealm(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetRegistry();
        RealmlistCacheSingleton& realmlistCacheSingleton = registry->ctx<RealmlistCacheSingleton>();

        u16 realmID = 0;
        if (!packet->payload->GetU16(realmID))
            return false;

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();

        auto itr = realmlistCacheSingleton.realmlistMap.find(realmID);
        if (itr == realmlistCacheSingleton.realmlistMap.end())
        {
            // Realm does not exist, send back "SMSG_SEND_ADDRESS" to tell the client theres no available server

            if (!PacketUtils::Write_SMSG_SEND_ADDRESS(buffer, 0))
                return false;
            
            netClient->Send(buffer);
        }
        else
        {
            if (!PacketUtils::Write_MSG_REQUEST_ADDRESS(buffer, AddressType::REALM, netClient->GetEntity(), reinterpret_cast<u8*>(&realmID), sizeof(u16)))
                return false;

            auto& connection = registry->ctx<ConnectionSingleton>();
            connection.netClient->Send(buffer);
        }

        return true;
    }
}