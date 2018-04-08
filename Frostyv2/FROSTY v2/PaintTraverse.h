#pragma once
#include "HookIncludes.h"
#include "Interfaces.h"
#include <ctime>
#include "ESP.h"
#include "MovementRecorder.h"

 


typedef void(__thiscall* paint_traverse_t)(PVOID, unsigned int, bool, bool);


bool once = true;
bool once1 = false;
int width1 = 0;
int height1 = 0;
void __fastcall hkPaintTraverse(PVOID pPanels, int edx, unsigned int vguiPanel, bool forceRepaint, bool allowForce)
{
	static auto ofunc = hooks::panel.get_original<paint_traverse_t>(41);
	C_BaseEntity* local = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	if (local != nullptr && local->IsAlive() && g_Options.Visuals.noscopeborder && !strcmp("HudZoom", g_Panel->GetName(vguiPanel)))
	{
		return;
	}
	ofunc(pPanels, vguiPanel, forceRepaint, allowForce);
	static unsigned int FocusOverlayPanel = 0;
	static bool FoundPanel = false;
	if (!FoundPanel)
	{
		PCHAR szPanelName = (PCHAR)g_Panel->GetName(vguiPanel);
		if (strstr(szPanelName, "FocusOverlayPanel"))
		{
			FocusOverlayPanel = vguiPanel;
			FoundPanel = true;
		}
	}
	else if (FocusOverlayPanel == vguiPanel)
	{
		if (G::menuon)
		{
			int width = 0;
			int height = 0;
			g_Engine->GetScreenSize(width, height);
			g_Render->Clear(0, 0, width, height, Color(50, 50, 50, 100));
		}
		if (g_Engine->IsConnected() && g_Engine->IsInGame())
		{
			static auto linegoesthrusmoke = U::FindPattern("client.dll", (PBYTE)"\x55\x8B\xEC\x83\xEC\x08\x8B\x15\x00\x00\x00\x00\x0F\x57\xC0", "xxxxxxxx????xxx");
			static auto smokecout = *(DWORD*)(linegoesthrusmoke + 0x8);
			if (g_Options.Visuals.NoSmoke) *(int*)(smokecout) = 0;

			visuals::instance().OnPaintTraverse(local);

			auto m_flFlashDuration = NetVarManager->GetOffset("DT_CSPlayer", "m_flFlashDuration");
			auto m_flFlashMaxAlpha = NetVarManager->GetOffset("DT_CSPlayer", "m_flFlashMaxAlpha");
			if (local != nullptr)
			{
				CBaseCombatWeapon* pWeapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(local->GetActiveWeaponHandle());
				if (g_Options.Visuals.NoFlash)
				{
					*MakePtr(float*, local, m_flFlashDuration) = 0.f;
					*MakePtr(float*, local, m_flFlashMaxAlpha) = 0.f;
				}
				if (local && local->IsScoped() && g_Options.Visuals.noscopeborder && MiscFunctions::IsSniper(pWeapon))
				{
					int width = 0;
					int height = 0;
					g_Engine->GetScreenSize(width, height);

					int centerX = static_cast<int>(width * 0.5f);
					int centerY = static_cast<int>(height * 0.5f);
					g_Render->Line(0, centerY, width, centerY, Color(0, 0, 0, 255));
					g_Render->Line(centerX, 0, centerX, height, Color(0, 0, 0, 255));
				}
			}
		}
		if (!g_Engine->IsInGame()) {
			g_Options.Misc.NameChangerFix = false;
			g_Options.Misc.NameSpammer = 0;
			g_Options.Misc.NoName = false;
			g_Options.Misc.ChatSpamMode = false;
		}
		if (g_Options.Ragebot.MainSwitch && !once)
		{
			g_Options.Legitbot.MainSwitch = false;
			once = !once;
		}
		if (g_Options.Legitbot.MainSwitch && once)
		{
			g_Options.Ragebot.MainSwitch = false;
			once = !once;
		}
		if (g_Options.Visuals.Time)
		{
			std::time_t result = std::time(nullptr);
			g_Render->Text(10, 6, Color(255, 255, 255, 255), g_Render->font.ESP, std::asctime(std::localtime(&result)));
		}
		if (g_Options.Visuals.AAIndicator)
		{
			int W, H, cW, cH, rW;
			g_Engine->GetScreenSize(W, H);
			cW = (W / 2) - 85;
			cH = H / 2;
			if (g_Options.Legitbot.EnableLegitAA && (g_Options.Legitbot.AAAngle > 0 && g_Options.Legitbot.AAAngle < 179)) // RIGHT
			{
				g_Render->Text(cW + 400, cH + 450, Color(150, 210, 0, 220), g_Render->font.ESP, L"RIGHT");
				g_Render->Text(cW - 400, cH + 450, Color(20, 20, 20, 160), g_Render->font.ESP, L"LEFT");
			}
			if (g_Options.Legitbot.EnableLegitAA && (g_Options.Legitbot.AAAngle < 0 && g_Options.Legitbot.AAAngle > -179)) // LEFT
			{
				g_Render->Text(cW + 400, cH + 450, Color(20, 20, 20, 160), g_Render->font.ESP, L"RIGHT");
				g_Render->Text(cW - 400, cH + 450, Color(150, 210, 0, 220), g_Render->font.ESP, L"LEFT");
			}
			if (g_Options.Legitbot.EnableLegitAA && (g_Options.Legitbot.AAAngle > 179 || g_Options.Legitbot.AAAngle < -179)) // BACKWARDS
			{
				g_Render->Text(cW, cH + 450, Color(150, 210, 0, 220), g_Render->font.ESP, L"BACKWARD");
			}
			if (g_Options.Legitbot.EnableLegitAA && (g_Options.Legitbot.AAAngle > -1 && g_Options.Legitbot.AAAngle < 1)) // Normal
			{
				g_Render->Text(cW, cH + 450, Color(20, 20, 20, 160), g_Render->font.ESP, L"DISABLED");
			}
			if (!g_Options.Legitbot.EnableLegitAA) // DISABLED
			{
				g_Render->Text(cW, cH + 450, Color(20, 20, 20, 160), g_Render->font.ESP, L"DISABLED");
			}
		}
	
	}
}


