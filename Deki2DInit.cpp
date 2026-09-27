#include "Deki2DInit.h"
#include "FrameAnimationMsgPack.h"

// Global scope, matching Deki2DInit.h - see the comment there.
void Deki2D_InitSystem()
{
    Deki2D::RegisterAnimationLoader();
}

void Deki2D_ShutdownSystem()
{
}
