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
        netPacketHandler->SetMessageHandler(Opcode::CMSG_CONNECTED, { ConnectionStatus::AUTH_SUCCESS, 0, GeneralHandlers::HandleConnected });
        netPacketHandler->SetMessageHandler(Opcode::CMSG_REQUEST_REALMLIST, { ConnectionStatus::CONNECTED, 0, GeneralHandlers::HandleRequestRealmlist });
        netPacketHandler->SetMessageHandler(Opcode::CMSG_SELECT_REALM, { ConnectionStatus::CONNECTED, 1, GeneralHandlers::HandleSelectRealm });
    }

    bool GeneralHandlers::HandleConnected(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        buffer->Put(Opcode::SMSG_CONNECTED);
        buffer->PutU16(0);
        netClient->Send(buffer);

        netClient->SetConnectionStatus(ConnectionStatus::CONNECTED);
        return true;
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

        u8 realmIndex = 0;
        if (!packet->payload->GetU8(realmIndex))
            return false;

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();

        auto itr = realmlistCacheSingleton.realmlistMap.find(realmIndex);
        if (itr == realmlistCacheSingleton.realmlistMap.end())
        {
            // Realm does not exist, send back "SMSG_SEND_ADDRESS" to tell the client theres no available server

            if (!PacketUtils::Write_SMSG_SEND_ADDRESS(buffer, 0))
                return false;
            
            netClient->Send(buffer);
        }
        else
        {
            if (!PacketUtils::Write_MSG_REQUEST_ADDRESS(buffer, AddressType::REALM, netClient->GetEntity(), &realmIndex, 1))
                return false;

            auto& connection = registry->ctx<ConnectionSingleton>();
            connection.netClient->Send(buffer);
        }

        return true;
    }
}