#include "EngineLoop.h"
#include <thread>
#include <Utils/Timer.h>
#include "Utils/ServiceLocator.h"
#include <Networking/NetPacketHandler.h>
#include <tracy/Tracy.hpp>

// Component Singletons
#include "ECS/Components/Singletons/TimeSingleton.h"
#include "ECS/Components/Singletons/DBSingleton.h"
#include "ECS/Components/Singletons/RealmlistCacheSingleton.h"
#include "ECS/Components/Network/ConnectionSingleton.h"
#include "ECS/Components/Network/ConnectionDeferredSingleton.h"
#include "ECS/Components/Network/AuthenticationSingleton.h"

// Components

// Systems
#include "ECS/Systems/RealmlistCacheSystem.h"
#include "ECS/Systems/Network/ConnectionSystems.h"

// Handlers
#include "Network/Handlers/Self/Auth/AuthHandlers.h"
#include "Network/Handlers/Self/GeneralHandlers.h"
#include "Network/Handlers/Client/Auth/AuthHandlers.h"
#include "Network/Handlers/Client/GeneralHandlers.h"

EngineLoop::EngineLoop()
    : _isRunning(false), _inputQueue(256), _outputQueue(16)
{
    _network.client = std::make_shared<NetClient>();
    _network.client->Init(NetSocket::Mode::TCP);

    std::shared_ptr<NetSocket> clientSocket = _network.client->GetSocket();
    clientSocket->SetBlockingState(false);
    clientSocket->SetNoDelayState(true);
    clientSocket->SetSendBufferSize(8192);
    clientSocket->SetReceiveBufferSize(8192);

    _network.server = std::make_shared<NetServer>();
}

EngineLoop::~EngineLoop()
{
}

void EngineLoop::Start()
{
    if (_isRunning)
        return;

    std::thread threadRun = std::thread(&EngineLoop::Run, this);
    threadRun.detach();
}

void EngineLoop::Stop()
{
    if (!_isRunning)
        return;

    Message message;
    message.code = MSG_IN_EXIT;
    PassMessage(message);
}

void EngineLoop::PassMessage(Message& message)
{
    _inputQueue.enqueue(message);
}

bool EngineLoop::TryGetMessage(Message& message)
{
    return _outputQueue.try_dequeue(message);
}

void EngineLoop::Run()
{
    _isRunning = true;

    SetupUpdateFramework();

    TimeSingleton& timeSingleton = _updateFramework.gameRegistry.set<TimeSingleton>();

    DBSingleton& dbSingleton = _updateFramework.gameRegistry.set<DBSingleton>();
    dbSingleton.auth.Connect("localhost", 3306, "root", "ascent", "novuscore", 0);

    RealmlistCacheSingleton& realmlistCacheSingleton = _updateFramework.gameRegistry.set<RealmlistCacheSingleton>();
    ConnectionSingleton& connectionSingleton = _updateFramework.gameRegistry.set<ConnectionSingleton>();
    ConnectionDeferredSingleton& connectionDeferredSingleton = _updateFramework.gameRegistry.set<ConnectionDeferredSingleton>();
    AuthenticationSingleton& authenticationSingleton = _updateFramework.gameRegistry.set<AuthenticationSingleton>();

    connectionSingleton.netClient = _network.client;
    bool didConnect = connectionSingleton.netClient->Connect("127.0.0.1", 8000);
    ConnectionUpdateSystem::Self_HandleConnect(connectionSingleton.netClient, didConnect);

    connectionDeferredSingleton.netServer = _network.server;
    connectionDeferredSingleton.netServer->SetOnConnectCallback(ConnectionUpdateSystem::Server_HandleConnect);
    if (!_network.server->Init(NetSocket::Mode::TCP, "127.0.0.1", 4000))
    {
        DebugHandler::PrintFatal("Network : Failed to initialize server (NovusCore - Auth)");
    }

    Timer timer;
    f32 targetDelta = 1.0f / 5.0f;
    while (true)
    {
        f32 deltaTime = timer.GetDeltaTime();
        timer.Tick();

        timeSingleton.lifeTimeInS = timer.GetLifeTime();
        timeSingleton.lifeTimeInMS = timeSingleton.lifeTimeInS * 1000;
        timeSingleton.deltaTime = deltaTime;

        if (!Update())
            break;

        {
            ZoneScopedNC("WaitForTickRate", tracy::Color::AntiqueWhite1)

            // Wait for tick rate, this might be an overkill implementation but it has the even tickrate I've seen - MPursche
            {
                ZoneScopedNC("Sleep", tracy::Color::AntiqueWhite1) for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta - 0.0025f; deltaTime = timer.GetDeltaTime())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            {
                ZoneScopedNC("Yield", tracy::Color::AntiqueWhite1) for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta; deltaTime = timer.GetDeltaTime())
                {
                    std::this_thread::yield();
                }
            }
        }


        FrameMark
    }

    // Clean up stuff here

    Message exitMessage;
    exitMessage.code = MSG_OUT_EXIT_CONFIRM;
    _outputQueue.enqueue(exitMessage);
}

bool EngineLoop::Update()
{
    ZoneScopedNC("Update", tracy::Color::Blue2);
    {
        ZoneScopedNC("HandleMessages", tracy::Color::Green3);
        Message message;

        while (_inputQueue.try_dequeue(message))
        {
            if (message.code == -1)
                assert(false);

            if (message.code == MSG_IN_EXIT)
            {
                return false;
            }
            else if (message.code == MSG_IN_PING)
            {
                ZoneScopedNC("Ping", tracy::Color::Green3);
                Message pongMessage;
                pongMessage.code = MSG_OUT_PRINT;
                pongMessage.message = new std::string("PONG!");
                _outputQueue.enqueue(pongMessage);
            }
        }
    }

    UpdateSystems();
    return true;
}

void EngineLoop::SetupUpdateFramework()
{
    tf::Framework& framework = _updateFramework.framework;
    entt::registry& registry = _updateFramework.gameRegistry;

    ServiceLocator::SetRegistry(&registry);
    SetMessageHandler();

    // RealmlistCacheSystem
    tf::Task realmlistCacheSystemTask = framework.emplace([&registry]()
    {
        ZoneScopedNC("RealmlistCacheSystem::Update", tracy::Color::Blue2)
        RealmlistCacheSystem::Update(registry);
    });

    // ConnectionUpdateSystem
    tf::Task connectionUpdateSystemTask = framework.emplace([&registry]()
    {
        ZoneScopedNC("ConnectionUpdateSystem::Update", tracy::Color::Blue2)
        ConnectionUpdateSystem::Update(registry);
    });
    connectionUpdateSystemTask.gather(realmlistCacheSystemTask);

    // ConnectionDeferredSystem
    tf::Task connectionDeferredSystemTask = framework.emplace([&registry]()
    {
        ZoneScopedNC("ConnectionDeferredSystem::Update", tracy::Color::Blue2)
        ConnectionDeferredSystem::Update(registry);
    });
    connectionDeferredSystemTask.gather(connectionUpdateSystemTask);
}
void EngineLoop::SetMessageHandler()
{
    NetPacketHandler* selfNetPacketHandler = new NetPacketHandler();
    ServiceLocator::SetSelfNetPacketHandler(selfNetPacketHandler);
    InternalSocket::AuthHandlers::Setup(selfNetPacketHandler);
    InternalSocket::GeneralHandlers::Setup(selfNetPacketHandler);

    NetPacketHandler* clientNetPacketHandler = new NetPacketHandler();
    ServiceLocator::SetClientNetPacketHandler(clientNetPacketHandler);
    Client::AuthHandlers::Setup(clientNetPacketHandler);
    Client::GeneralHandlers::Setup(clientNetPacketHandler);
}
void EngineLoop::UpdateSystems()
{
    ZoneScopedNC("UpdateSystems", tracy::Color::Blue2)
    {
        ZoneScopedNC("Taskflow::Run", tracy::Color::Blue2)
            _updateFramework.taskflow.run(_updateFramework.framework);
    }
    {
        ZoneScopedNC("Taskflow::WaitForAll", tracy::Color::Blue2)
            _updateFramework.taskflow.wait_for_all();
    }
}
