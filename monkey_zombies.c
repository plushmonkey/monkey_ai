#include "asss.h"
#include "monkey_ai.h"
#include "monkey_pathing.h"

/* Test code at the moment */

local Imodman *mm;
local Iarenaman *aman;
local Ichat *chat;
local Icmdman *cmd;
local Igame *game;
local Iplayerdata *pd;
local Iconfig *config;
local Iai *ai;
local Ilogman *lm;
local Ipathing *path;
local Inet *net;

local AIPlayer *test_player;

local int GetInterfaces(Imodman *mm_) {
    mm = mm_;

    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    config = mm->GetInterface(I_CONFIG, ALLARENAS);
    ai = mm->GetInterface(I_AI, ALLARENAS);
    path = mm->GetInterface(I_PATHING, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);

    if (!(lm && aman && chat && cmd && game && pd && config && ai && path))
        mm = NULL;
    return mm != NULL;
}

local void ReleaseInterfaces(Imodman* mm_) {
    mm_->ReleaseInterface(net);
    mm_->ReleaseInterface(path);
    mm_->ReleaseInterface(ai);
    mm_->ReleaseInterface(config);
    mm_->ReleaseInterface(pd);
    mm_->ReleaseInterface(game);
    mm_->ReleaseInterface(cmd);
    mm_->ReleaseInterface(chat);
    mm_->ReleaseInterface(aman);
    mm_->ReleaseInterface(lm);
    mm = NULL;
}

local int ZBulletDamage(AIPlayer *aip, EnemyWeapon *weapon) {
    lm->Log(L_INFO, "Zombie hit by bullet. Ignoring damage.");
    return 0;
}

local void OnBombExplosion(EnemyWeapon *weapon) {
    
}

EXPORT const char info_zombies[] = "zombies v0.1 by monkey\n";
EXPORT int MM_zombies(int action, Imodman *mm_, Arena* arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
        {
            if (!GetInterfaces(mm_)) {
                ReleaseInterfaces(mm_);
                break;
            }

            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
        {
            ReleaseInterfaces(mm_);
            rv = MM_OK;
        }
        break;
        case MM_ATTACH:
        {
            test_player = ai->CreateAI(arena, "*zombie*", 100, 1);
            
            //ai->SetDamageFunction(test_player, W_BULLET, ZBulletDamage);
            //ai->SetDamageFunction(test_player, W_BOUNCEBULLET, ZBulletDamage);
            
            mm->RegCallback(CB_BOMBEXPLOSION, OnBombExplosion, arena);
            
            LinkedList *pathlist = path->FindPath(arena, 512, 512, 545, 535);
            Node *p;
            Link *link;
            
            FOR_EACH(pathlist, p, link) {
                lm->Log(L_INFO, "Path: %d, %d", p->x, p->y);
            }
            
            lm->Log(L_INFO, "Path size: %d", LLCount(pathlist));

            LLFree(pathlist);
            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            mm->UnregCallback(CB_BOMBEXPLOSION, OnBombExplosion, arena);
            ai->DestroyAI(test_player);
            rv = MM_OK;
        }
        break;
    }

    return rv;
}
