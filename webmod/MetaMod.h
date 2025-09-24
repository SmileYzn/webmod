#pragma once

#pragma region DLL_PRE
#pragma endregion

#pragma region DLL_POST
void DLL_POST_ServerActivate(edict_t *pEdictList, int edictCount, int clientMax);
void DLL_POST_ServerDeactivate();
void DLL_POST_StartFrame();
#pragma endregion

#pragma region NEW_DLL_PRE
#pragma endregion

#pragma region NEW_DLL_POST
#pragma endregion

#pragma region ENGINE_PRE
#pragma endregion

#pragma region ENGINE_POST
#pragma endregion