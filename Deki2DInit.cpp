#include "Deki2DInit.h"
#include "FrameAnimationMsgPack.h"

// Global scope, matching Deki2DInit.h - see the comment there.
void Deki2DInitSystem()
{
    Deki2D::RegisterAnimationLoader();
}

void Deki2DShutdownSystem()
{
}
