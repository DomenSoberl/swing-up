#include <Windows.h>
#include <string>
#include <math.h>
#include "engine.h"

extern "C" {
#include "ddpgc.h"
}

#define DLLEXPORT extern "C" __declspec(dllexport)

BOOL APIENTRY DllMain(
    HINSTANCE hinstDLL,
    DWORD fdwReason,
    LPVOID lpReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

#define PI 3.14159265358979323846

/* The number of episodes and the length of one episode. */
#define EPISODES 300
#define EPISODE_LENGTH 300

/* Global training variables. */
DDPG* ddpg = nullptr;
int steps = 0;
int episode = 0;
double episodeReward = 0;
bool training;

/* The last known state. */
SimulationState lastState;

/* The last executed action. */
double lastAction;

/* Logging  */
char logBuffer[1024];

void Log(std::string s)
{
    strcat_s(logBuffer, 1024, (s).c_str());
}

/* The reward function. */
double computeReward(SimulationState state)
{
    double cost = pow(state.theta, 2) + 0.1 * pow(state.dtheta, 2) + 0.001 * pow(state.x, 2);
    return -cost;
}

/* Called when the simulator is started. */
DLLEXPORT void simulatorInitialize(SimulatorParameters& simulatorParameters)
{
    /* Initialize the engine. */
    simulatorParameters.pLogBuffer = logBuffer;
    simulatorParameters.engineName = "Swing up";
    simulatorParameters.logFilename = "log.txt";

    /* Set the simulator parameters. */
    static const Marker markers[] = {
        {0, 0.1, 192, 192, 64}, // {x, width, red, green, blue}
        {0, 0, 0, 0, 0} // Array must end with a zero-marker.
    };
    simulatorParameters.windowWidth = 1600;
    simulatorParameters.windowHeight = 400;
    simulatorParameters.markers = markers;
    
    /* initialize the DDPG method. */
    ddpg_init();
    int layers[2] = { 128, 64 };
    ddpg = ddpg_create(3, 1, NULL, 2, layers, 2, layers, 100000, 32);

    /* No action has yet been executed. */
    lastAction = 0;

    /* Try to load the pre-trained model. */
    bool modelLoaded = (ddpg_load_policy(ddpg, "model.ddpg") == 0);

    /* By default, we do not train when a pre-trained model exists. */
    training = !modelLoaded;

    /* If called with the 'train' parameter, we train even if a pre-trained model exists. */
    if (simulatorParameters.argc > 0 && (strcmp(simulatorParameters.argv[0], "train") == 0))
        training = true;

    /* Print out a message on whether we are training or playing. */
    if (training) {
        if (modelLoaded)
            Log("Continuing training a pre-trained model.\n");
        else
            Log("Training from scratch.\n");
    }
    else {
        Log("Using a pre-trained model.\n");
    }
    
    /* Speed up training. */
    if (training) {
        simulatorParameters.simulationSpeed = 4;
    }
}

/* Called when the simulator is being shut down. */
DLLEXPORT void simulatorShutdown()
{
    if (training) {
        if (ddpg_save_policy(ddpg, "model.ddpg") == 0)
            Log("Model saved.\n");
        else
            Log("Failed to save the model.\n");
    }
    ddpg_destroy(ddpg);
}

/* Called when the simulation is being reset. */
DLLEXPORT void setInitialState(InitialState& initialState)
{
    initialState.theta = deepc_random_double(-PI/2, PI/2) + PI;
    episodeReward = 0;

    Log("Episode " + std::to_string(episode + 1) + "/" + std::to_string(EPISODES) + " ");
}

/* Called every time the state changes. */
DLLEXPORT void stateUpdated(double simulationTime, SimulationState simulationState, SimulationParameters& simulationParameters)
{
    /* Let a dot denote a progress of 50 steps. */
    if (steps % 50 == 0)
        Log(".");

    /* If the episode has ended. */
    if (steps >= EPISODE_LENGTH - 1) {
        Log(" reward: " + std::to_string(episodeReward / EPISODE_LENGTH) + "\n");

        ddpg_update_target_networks(ddpg);
        ddpg_new_episode(ddpg);

        steps = 0;
        episodeReward = 0;
        
        /* If all episodes are done, the simulation stops, otherwise just resets. */
        if (++episode >= EPISODES)
            simulationParameters.simulationAction = TERMINATE_SIMULATION;
        else
            simulationParameters.simulationAction = RESET_SIMULATION;
        
        return;
    }

    /* Remember the last state. */
    lastState = simulationState;
    
    steps++;
}

/* Called when the simulator expects the engine to apply and action.
   This is usually immediatelly after the stateUpdate call. */
DLLEXPORT void applyAction(CartAction& cartAction)
{
    /* Convert the state to the DDPG format. */
    double ddpgState[3] = {
            lastState.theta,
            lastState.dtheta,
            lastState.x
    };

    /* Compute the reward obtained by entering the current state. */
    double reward = computeReward(lastState);
    episodeReward += reward;

    /* If we are training, let DDPG observe the current experience. */
    if (training)
        ddpg_observe(ddpg, &lastAction, reward, ddpgState, 0);
    
    /* Decide what action to take. */
    lastAction = *ddpg_action(ddpg, ddpgState);
    cartAction.force = 10 * lastAction;

    /* If we are training, train the model. */
    if (training)
        ddpg_train(ddpg, 0.99);
}

/* Called when user pressed a key. */
DLLEXPORT int keyPressed(KeyInfo& keyInfo)
{
    return 0;
}