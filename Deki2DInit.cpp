#include "Deki2DInit.h"
#include "Deki2DPackage.h"
#include "FrameAnimationMsgPack.h"

// In ClipComponent.cpp and SortingGroupComponent.cpp, which hold nothing else
// a firmware link would keep: calling them here keeps those files, so clips
// and sorting groups work on boards too.
#ifdef DEKI_FEATURE_CLIP
void Deki2DRegisterClipAdapters();
#endif
#ifdef DEKI_FEATURE_SORTING_GROUP
void Deki2DRegisterSortingGroupAdapters();
#endif

// Global scope, matching Deki2DInit.h - see the comment there.
void Deki2DInitSystem()
{
    Deki2D::RegisterAnimationLoader();
#ifdef DEKI_FEATURE_CLIP
    Deki2DRegisterClipAdapters();
#endif
#ifdef DEKI_FEATURE_SORTING_GROUP
    Deki2DRegisterSortingGroupAdapters();
#endif
}

void Deki2DShutdownSystem()
{
}
