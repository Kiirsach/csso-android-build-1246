//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HUD overhead name display for teammates
//
//=============================================================================//

#include "cbase.h"
#include "cs_hud_headname.h"
#include "iclientmode.h"
#include "clientmode_shared.h"
#include "c_baseplayer.h"
#include "c_basecombatweapon.h"
#include "c_cs_player.h"
#include "cs_gamerules.h"
#include "hud_macros.h"
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include "vgui_controls/Label.h"
#include "vgui_controls/VectorImagePanel.h"
#include "filesystem.h"
#include "utlmap.h"
#include "utlstring.h"
#include "tier0/memdbgon.h"

extern int ScreenTransform(const Vector &point, Vector &screen);

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
static ConVar cl_headname(
    "cl_headname", "1", FCVAR_ARCHIVE,
    "Show overhead name panels for teammates (0=off, 1=on)",
    true, 0, true, 1);


//-----------------------------------------------------------------------------
// Layout constants
//-----------------------------------------------------------------------------
static const int   HN_W             = 180;    
static const int   HN_ROW_H         = 18;     
static const int   HN_ICON_H        = 15;     
static const int   HN_ICON_GAP      = 2;      
static const int   HN_HP_H          = 15;     
static const int   HN_ARROW_H       = 8;      
static const int   HN_PAD           = 2;      
static const int   HN_ROW_GAP       = 1;      

// Distance — opacity
static const float HN_DIST_CLOSE    = 200.0f;
static const float HN_DIST_FAR      = 800.0f;

// Opacity values
static const int   HN_ALPHA_CLOSE   = 255;
static const int   HN_ALPHA_FAR     = 155;

// Distance — visibility limit
static const float HN_DIST_MAX_SHOW = 1500.0f;
static const int   HN_EDGE_MARGIN   = 20;

//-----------------------------------------------------------------------------
// SVG existence cache
//-----------------------------------------------------------------------------
static bool SVGFileExists(const char *pPath)
{
    static CUtlMap<CUtlString, bool> s_svgCache(DefLessFunc(CUtlString));

    CUtlString key(pPath);
    unsigned short idx = s_svgCache.Find(key);
    if (idx != s_svgCache.InvalidIndex())
        return s_svgCache[idx];

    bool bExists = g_pFullFileSystem->FileExists(pPath, "GAME");
    s_svgCache.Insert(key, bExists);
    return bExists;
}

//-----------------------------------------------------------------------------
// Get SVG path for weapon
//-----------------------------------------------------------------------------
static const char* GetWeaponSVGPath(const char *pWeaponClass)
{
    static char path[128];
    const char *pShort = Q_strstr(pWeaponClass, "weapon_") ? pWeaponClass + 7 : pWeaponClass;
    Q_snprintf(path, sizeof(path), "materials/vgui/weapons/svg/%s.svg", pShort);

    return SVGFileExists(path) ? path : NULL;
}

static bool IsKnifeWeapon(const char *pWeaponClass)
{
    return Q_stristr(pWeaponClass, "knife") != NULL;
}

//-----------------------------------------------------------------------------
// CPlayerNamePanel
//-----------------------------------------------------------------------------
CPlayerNamePanel::CPlayerNamePanel(vgui::Panel *pParent)
    : vgui::EditablePanel(pParent, "PlayerNamePanel")
{
    SetVisible(false);
    SetPaintBackgroundEnabled(false);
    SetPaintBorderEnabled(false);

    vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(
        vgui::scheme()->GetScheme("ClientScheme"));
    m_hFont = pScheme ? pScheme->GetFont("HeadName", true) : vgui::INVALID_FONT;
    m_hFontSmall = pScheme ? pScheme->GetFont("Default", true) : vgui::INVALID_FONT;
    if (m_hFont == vgui::INVALID_FONT)
        Warning("CPlayerNamePanel: Could not load 'HeadName' font\n");

    m_pNameLabel = new vgui::Label(this, "Name", "");
    m_pNameLabel->SetFont(m_hFont);
    m_pNameLabel->SetPaintBackgroundEnabled(false);
    m_pNameLabel->SetContentAlignment(vgui::Label::a_west);

    m_pHPLabel = new vgui::Label(this, "HP", "");
    m_pHPLabel->SetFont(m_hFont);
    m_pHPLabel->SetPaintBackgroundEnabled(false);
    m_pHPLabel->SetContentAlignment(vgui::Label::a_west);

    m_pArrowLabel = new vgui::Label(this, "Arrow", "");
    m_pArrowLabel->SetFont(m_hFont);
    m_pArrowLabel->SetPaintBackgroundEnabled(false);
    m_pArrowLabel->SetContentAlignment(vgui::Label::a_center); 
    {
        wchar_t arrow[] = { 0x25BC, 0 }; 
        m_pArrowLabel->SetText(arrow);
    }

    for (int i = 0; i < 6; i++)
    {
        char name[32];
        Q_snprintf(name, sizeof(name), "WeaponIcon%d", i);
        m_pWeaponIcons[i] = new vgui::VectorImagePanel(this, name);
        m_pWeaponIcons[i]->SetVisible(false);
        m_pWeaponIcons[i]->SetFgColor(Color(255, 255, 255, 255));
    }

    m_hLastPrimary  = NULL;
    m_hLastPistol   = NULL;
    m_hLastC4       = NULL;
    m_bHasDefuser   = false;
    m_bHasC4        = false;
    for (int i = 0; i < 3; i++)
        m_hLastGrenades[i] = NULL;

    m_nCachedHP      = -1;
    m_nLastAlpha     = -1;
    m_szCachedName[0] = '\0';
    m_nLastIconCount = -1;
    m_nLastPanelW    = -1;
    m_nHeadBone      = -2;

    SetSize(HN_W, 100);
}

void CPlayerNamePanel::Reset()
{
    SetVisible(false);
    m_hLastPrimary  = NULL;
    m_hLastPistol   = NULL;
    m_hLastC4       = NULL;
    m_bHasDefuser   = false;
    m_bHasC4        = false;
    for (int i = 0; i < 3; i++)
        m_hLastGrenades[i] = NULL;

    m_nCachedHP      = -1;
    m_nLastAlpha     = -1;
    m_szCachedName[0] = '\0';
    m_nLastIconCount = -1;
    m_nLastPanelW    = -1;
    m_nHeadBone      = -2;
    m_hBoneCachePlayer = NULL;

    for (int i = 0; i < 6; i++)
    {
        if (m_pWeaponIcons[i])
            m_pWeaponIcons[i]->SetVisible(false);
    }
}

void CPlayerNamePanel::Update(C_CSPlayer *pPlayer, int screenX, int screenY)
{
    if (!pPlayer)
    {
        SetVisible(false);
        return;
    }

    C_CSPlayer *pLocal = C_CSPlayer::GetLocalCSPlayer();
    float flDistance = 0.0f;

    if (pLocal)
    {
        flDistance = (pPlayer->EyePosition() - pLocal->EyePosition()).Length();
    }

    if (flDistance > HN_DIST_MAX_SHOW)
    {
        SetVisible(false);
        return;
    }

    int iAlpha = HN_ALPHA_CLOSE;
    if (flDistance > HN_DIST_CLOSE)
    {
        float t = clamp((flDistance - HN_DIST_CLOSE) / (HN_DIST_FAR - HN_DIST_CLOSE), 0.0f, 1.0f);
        iAlpha = (int)Lerp(t, (float)HN_ALPHA_CLOSE, (float)HN_ALPHA_FAR);
    }
    iAlpha = clamp(iAlpha, HN_ALPHA_FAR, HN_ALPHA_CLOSE);

    bool bAlphaChanged = (iAlpha != m_nLastAlpha);

    C_BaseCombatWeapon *pPrimary    = NULL;
    C_BaseCombatWeapon *pPistol     = NULL;
    C_BaseCombatWeapon *pC4         = NULL;
    C_BaseCombatWeapon *pGrenade[3] = { NULL, NULL, NULL };
    int grenadeCount = 0;
    bool bHasDefuser = pPlayer->HasDefuser();

    for (int i = 0; i < MAX_WEAPONS; i++)
    {
        C_BaseCombatWeapon *pWep = pPlayer->GetWeapon(i);
        if (!pWep) continue;

        const char *pClass = pWep->GetClassname();

        if (IsKnifeWeapon(pClass)) continue;

        if (Q_stristr(pClass, "weapon_c4"))
        {
            pC4 = pWep;
            continue;
        }

        int slot = pWep->GetSlot();

        if (slot == 0 && !pPrimary)
            pPrimary = pWep;
        else if (slot == 1 && !pPistol)
            pPistol = pWep;
        else if ((slot == 3 || slot == 4) && grenadeCount < 3)
            pGrenade[grenadeCount++] = pWep;
    }

    if (pPrimary) pPistol = NULL;

    bool bWeaponsChanged =
        (pPrimary     != m_hLastPrimary.Get())     ||
        (pPistol      != m_hLastPistol.Get())      ||
        (pC4          != m_hLastC4.Get())          ||
        (bHasDefuser  != m_bHasDefuser)            ||
        (pGrenade[0]  != m_hLastGrenades[0].Get()) ||
        (pGrenade[1]  != m_hLastGrenades[1].Get()) ||
        (pGrenade[2]  != m_hLastGrenades[2].Get());

    if (m_nLastIconCount < 0 || m_nLastPanelW < 0)
        bWeaponsChanged = true;

    if (bWeaponsChanged)
    {
        m_hLastPrimary       = pPrimary;
        m_hLastPistol        = pPistol;
        m_hLastC4            = pC4;
        m_bHasDefuser        = bHasDefuser;
        m_bHasC4             = (pC4 != NULL);
        m_hLastGrenades[0]   = pGrenade[0];
        m_hLastGrenades[1]   = pGrenade[1];
        m_hLastGrenades[2]   = pGrenade[2];

        int iconCount = 0;

        auto SetIcon = [&](const char *pPath)
        {
            if (!pPath || iconCount >= 6) return;

            vgui::VectorImagePanel *pIcon = m_pWeaponIcons[iconCount];
            pIcon->SetTexture(pPath);

            int natW, natH;
            pIcon->GetSize(natW, natH);

            int dispW;
            if (natH > 0 && natW > 0)
                dispW = (natW * HN_ICON_H) / natH;
            else
                dispW = HN_ICON_H * 2;

            pIcon->SetSize(dispW, HN_ICON_H);
            pIcon->SetFgColor(Color(255, 255, 255, iAlpha));
            pIcon->SetVisible(true);
            iconCount++;
        };

        if (bHasDefuser) SetIcon("materials/vgui/hud/svg/defuser.svg");
        if (pC4)         SetIcon("materials/vgui/hud/svg/bomb_c4.svg");

        for (int i = 0; i < grenadeCount; i++)
        {
            if (pGrenade[i])
                SetIcon(GetWeaponSVGPath(pGrenade[i]->GetClassname()));
        }

        if (pPrimary) SetIcon(GetWeaponSVGPath(pPrimary->GetClassname()));
        else if (pPistol) SetIcon(GetWeaponSVGPath(pPistol->GetClassname()));

        for (int i = iconCount; i < 6; i++)
            m_pWeaponIcons[i]->SetVisible(false);
            
        m_nLastIconCount = iconCount;
    }
    else if (bAlphaChanged)
    {
        for (int i = 0; i < m_nLastIconCount; i++)
            m_pWeaponIcons[i]->SetFgColor(Color(255, 255, 255, iAlpha));
    }

    const char *pName = pPlayer->GetPlayerName();
    bool bNameChanged = (Q_strcmp(pName, m_szCachedName) != 0);

    if (bNameChanged)
    {
        m_pNameLabel->SetText(pName);
        Q_strncpy(m_szCachedName, pName, sizeof(m_szCachedName));
    }

    if (bNameChanged || bAlphaChanged)
    {
        bool bIsCT = (pPlayer->GetTeamNumber() == TEAM_CT);
        Color nameColor = bIsCT
            ? Color(150, 200, 255, iAlpha)   
            : Color(226, 212, 157, iAlpha);  
        m_pNameLabel->SetFgColor(nameColor);
    }

    int hp = clamp(pPlayer->GetHealth(), 0, 100);
    bool bHPChanged = (hp != m_nCachedHP);

    if (bHPChanged)
    {
        char hpText[16];
        Q_snprintf(hpText, sizeof(hpText), "%d%%", hp);
        m_pHPLabel->SetText(hpText);
        m_nCachedHP = hp;
    }

    if (bHPChanged || bAlphaChanged)
    {
        Color hpColor = (hp <= 30)
            ? Color(255, 80, 80, iAlpha)
            : Color(255, 255, 255, iAlpha);
        m_pHPLabel->SetFgColor(hpColor);
    }

    if (bAlphaChanged)
        m_pArrowLabel->SetFgColor(Color(255, 255, 255, iAlpha));

    m_nLastAlpha = iAlpha;

    m_pNameLabel->SizeToContents();
    m_pHPLabel->SizeToContents();
    
    m_pArrowLabel->SizeToContents(); 

    int nameW = m_pNameLabel->GetWide();
    int hpW   = m_pHPLabel->GetWide();
    int arrowW = m_pArrowLabel->GetWide();
    int arrowH = m_pArrowLabel->GetTall();
    int textGap = 6; 

    int requiredTextW = nameW + (textGap + hpW) * 2; 

    int totalIconWidth = 0;
    int visibleIconCount = 0;
    for (int i = 0; i < 6; i++) {
        if (m_pWeaponIcons[i]->IsVisible()) {
            totalIconWidth += m_pWeaponIcons[i]->GetWide() + (visibleIconCount > 0 ? HN_ICON_GAP : 0);
            visibleIconCount++;
        }
    }

    int panelW = max(totalIconWidth, requiredTextW) + HN_PAD * 2;

    int iconStartX = (panelW - totalIconWidth) / 2;
    int curX = iconStartX;
    for (int i = 0; i < 6; i++) {
        if (m_pWeaponIcons[i]->IsVisible()) {
            m_pWeaponIcons[i]->SetPos(curX, 0);
            curX += m_pWeaponIcons[i]->GetWide() + HN_ICON_GAP;
        }
    }

    int iconRowH   = (visibleIconCount > 0) ? HN_ICON_H + HN_ROW_GAP : 0;
    int textY      = iconRowH + 2; 

    int nameX = (panelW - nameW) / 2; 
    int hpX   = nameX + nameW + textGap; 

    m_pNameLabel->SetBounds(nameX, textY, nameW, HN_ROW_H);
    m_pHPLabel->SetBounds(hpX, textY, hpW, HN_ROW_H);

    int arrowY = textY + HN_ROW_H + HN_ROW_GAP;
    if (arrowW < 10) arrowW = 10;
    if (arrowH < 10) arrowH = 10;
    
    m_pArrowLabel->SetBounds((panelW - arrowW) / 2, arrowY, arrowW, arrowH);

    int totalH = arrowY + arrowH;
    SetSize(panelW, totalH);

    int posX = screenX - panelW / 2;
    int posY = screenY - GetTall() - 4;

    SetPos(posX, posY);
    SetVisible(true);
}

//-----------------------------------------------------------------------------
// CHudPlayerName
//-----------------------------------------------------------------------------
DECLARE_HUDELEMENT(CHudPlayerName);

CHudPlayerName::CHudPlayerName(const char *pElementName)
    : CHudElement(pElementName), vgui::EditablePanel(NULL, "HudPlayerName")
{
    vgui::Panel *pParent = g_pClientMode->GetViewport();
    SetParent(pParent);
    SetHiddenBits(HIDEHUD_PLAYERDEAD);
    SetPaintBackgroundEnabled(false);
    SetPaintBorderEnabled(false);

    memset(m_pPanels, 0, sizeof(m_pPanels));
}

void CHudPlayerName::Init()
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!m_pPanels[i])
        {
            m_pPanels[i] = new CPlayerNamePanel(this);
            m_pPanels[i]->SetVisible(false);
        }
    }
}

void CHudPlayerName::Reset()
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (m_pPanels[i])
            m_pPanels[i]->Reset();
    }
}

void CHudPlayerName::OnThink()
{
    int sw, sh;
    vgui::surface()->GetScreenSize(sw, sh);
    SetBounds(0, 0, sw, sh);

    if (!cl_headname.GetBool())
    {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (m_pPanels[i]) m_pPanels[i]->SetVisible(false);
        return;
    }

    if (mp_teammates_are_enemies.GetBool())
    {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (m_pPanels[i]) m_pPanels[i]->SetVisible(false);
        return;
    }

    C_CSPlayer *pLocal = C_CSPlayer::GetLocalCSPlayer();
    if (!pLocal)
    {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (m_pPanels[i]) m_pPanels[i]->SetVisible(false);
        return;
    }

    int localIdx = pLocal->entindex();

    int spectatedIdx = -1;
    if (!pLocal->IsAlive())
    {
        C_BaseEntity *pTarget = pLocal->GetObserverTarget();
        if (pTarget)
            spectatedIdx = pTarget->entindex();
    }

    for (int i = 1; i <= MAX_PLAYERS; i++)
    {
        CPlayerNamePanel *pPanel = m_pPanels[i - 1];
        if (!pPanel)
            continue;

        if (i == localIdx || i == spectatedIdx)
        {
            pPanel->SetVisible(false);
            continue;
        }

        C_CSPlayer *pPlayer = dynamic_cast<C_CSPlayer *>(UTIL_PlayerByIndex(i));

        if (!pPlayer || !pPlayer->IsAlive() ||
            pPlayer->GetTeamNumber() != pLocal->GetTeamNumber())
        {
            pPanel->SetVisible(false);
            continue;
        }

        int sx, sy;
        if (!GetHeadScreenPos(pPlayer, sx, sy, sw, sh))
        {
            pPanel->SetVisible(false);
            continue;
        }

        pPanel->Update(pPlayer, sx, sy);
    }
}

bool CHudPlayerName::GetHeadScreenPos(C_CSPlayer *pPlayer, int &sx, int &sy, int sw, int sh)
{
    Vector worldPos = pPlayer->GetRenderOrigin();
    worldPos.z += pPlayer->CollisionProp()->OBBMaxs().z + 8.0f;

    Vector screen;
    if (ScreenTransform(worldPos, screen) != 0)
        return false;

    sx = (int)(0.5f * (1.0f + screen.x) * sw);
    sy = (int)(0.5f * (1.0f - screen.y) * sh);

    if (sx < -HN_EDGE_MARGIN || sx > sw + HN_EDGE_MARGIN ||
        sy < -HN_EDGE_MARGIN || sy > sh + HN_EDGE_MARGIN)
        return false;

    return true;
}