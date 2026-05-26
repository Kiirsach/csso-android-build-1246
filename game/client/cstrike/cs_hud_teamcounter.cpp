//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HUD team counter with player avatars
//          Displays round timer, win counts, and per-player avatar tiles
//          with team outline + skull for dead players.
//          HP bar (delayed white overlay, team color) INSIDE avatar (bottom area).
//
//=============================================================================//

#include "cbase.h"
#include "iclientmode.h"
#include "hudelement.h"
#include "c_cs_player.h"
#include "c_cs_team.h"
#include "c_plantedc4.h"
#include "c_cs_playerresource.h"
#include "cs_gamerules.h"
#include "cs_shareddefs.h"
#include "vgui_avatarimage.h"
#include <vgui_controls/AnimationController.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/VectorImagePanel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>

extern CUtlVector<C_PlantedC4*> g_PlantedC4s;
#include "tier0/memdbgon.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
ConVar hud_playercount_pos("hud_playercount_pos", "0", FCVAR_ARCHIVE,
    "0 = top of screen, 1 = bottom of screen");

//-----------------------------------------------------------------------------
// Layout and color constants
//-----------------------------------------------------------------------------
#define TC_MAX_AVATAR_SLOTS 6
#define TC_AVATARS_PER_ROW 3
#define TC_BASE_AVATAR_SIZE 32
#define TC_BASE_AVATAR_GAP 3
#define TC_BASE_OUTLINE 2
#define TC_BASE_CENTER_W 84
#define TC_BASE_CENTER_GAP 6
#define TC_BASE_HP_BAR_HEIGHT  3    // slightly larger for internal look (px at 1080p)
#define TC_BASE_HP_BAR_MARGIN -1    // distance to avatar bottom inside

static const Color TC_CT_OUTLINE_COLOR  (150, 200, 255, 220);  // CT blue
static const Color TC_T_OUTLINE_COLOR   (226, 212, 157, 220);  // T yellow
static const Color TC_DEAD_BG_COLOR( 22, 24, 26, 220 );
static const Color TC_HP_BG_COLOR(0, 0, 0, 0);
static const Color TC_HP_DELAYED_COLOR  (255,255,255,180);     // Delayed (white) overlay
static const Color TC_HP_LOW_COLOR      (255, 60, 60, 255); // LOW HP RED

//-----------------------------------------------------------------------------
// Animated health bar (inside avatar)
//-----------------------------------------------------------------------------
class CHealthBarPanel : public vgui::Panel
{
    DECLARE_CLASS_SIMPLE(CHealthBarPanel, vgui::Panel);

public:
    CHealthBarPanel(Panel *parent, const char *name)
        : vgui::Panel(parent, name),
  m_ColorMain(TC_CT_OUTLINE_COLOR),
  m_ColorDelayed(TC_HP_DELAYED_COLOR),
  m_flTargetFrac(1.0f),
  m_flAnimFrac(1.0f),
  m_flAnimSpeed(1.5f),
  m_bDrawBG(false)
    {
        SetPaintBackgroundEnabled(false);
    }

    void SetHealthFraction(float frac) { m_flTargetFrac = clamp(frac, 0.f, 1.f); }
    void SetColors(Color main, Color delayed, Color bg, bool drawBg=true)
    {
        m_ColorMain = main;
        m_ColorDelayed = delayed;
        m_ColorBG = bg;
        m_bDrawBG = drawBg;
    }
    void TickAnim(float frameTime)
    {
        if (m_flAnimFrac > m_flTargetFrac)
        {
            m_flAnimFrac -= m_flAnimSpeed * frameTime;
            if (m_flAnimFrac < m_flTargetFrac)
                m_flAnimFrac = m_flTargetFrac;
            Repaint();
        }
        else
            m_flAnimFrac = m_flTargetFrac;
    }

protected:
    void Paint() override
    {
        int w, h; GetSize(w, h);

        // Draw black bar as background ("base"), unless禁用
        if (m_bDrawBG)
        {
            surface()->DrawSetColor(m_ColorBG);
            surface()->DrawFilledRect(0, 0, w, h);
        }

        int mainW = int(w * m_flTargetFrac + 0.5f);
        int delayedW = int(w * m_flAnimFrac + 0.5f);

        // Draw delayed (white, right) overlay
        if (delayedW > mainW)
        {
            surface()->DrawSetColor(m_ColorDelayed);
            surface()->DrawFilledRect(mainW, 0, delayedW, h);
        }
        // Draw team color bar
        surface()->DrawSetColor(m_ColorMain);
        surface()->DrawFilledRect(0, 0, mainW, h);
    }

    float m_flTargetFrac, m_flAnimFrac, m_flAnimSpeed;
    Color m_ColorMain, m_ColorDelayed, m_ColorBG;
    bool  m_bDrawBG;
};

//=============================================================================
//
// CHudTeamCounter
//
//=============================================================================
class CHudTeamCounter : public CHudElement, public EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHudTeamCounter, EditablePanel);

public:
    CHudTeamCounter(const char *pElementName);
    virtual ~CHudTeamCounter() {}

    virtual void Init(void);
    virtual void ApplySchemeSettings(IScheme *pScheme);
    virtual void PerformLayout();
    virtual void PaintBackground();
    virtual void Reset(void);
    virtual bool ShouldDraw();
    virtual void OnThink();

private:
    void    UpdateAvatarMode();
    void    Layout();
    int     ScalePx(float basePixels) const;

    Label           *m_pRoundTimerLabel;
    Label           *m_pCTWinCounterLabel;
    Label           *m_pTWinCounterLabel;
    VectorImagePanel *m_pBombIcon;

    struct AvatarSlot_t
    {
        CAvatarImagePanel *pAvatar;
        ImagePanel        *pSkull;
        CHealthBarPanel   *pHPBar;
        int                iLastPlayerIndex;
        bool               bIsAlive;
    };
    AvatarSlot_t m_CTSlots[TC_MAX_AVATAR_SLOTS];
    AvatarSlot_t m_TSlots[TC_MAX_AVATAR_SLOTS];

    struct SlotRect_t { int x, y, w, h; };
    SlotRect_t m_CTSlotRects[TC_MAX_AVATAR_SLOTS];
    SlotRect_t m_TSlotRects[TC_MAX_AVATAR_SLOTS];
    int  m_iNumActiveCTSlots;
    int  m_iNumActiveTSlots;
    int  m_iAvatarSize;
    int  m_iOutlineThick;
    int  m_iRoundTime;
    bool m_bIsAtTheBottom;

    CPanelAnimationVar(Color, m_clrC4Planted, "C4PlantedColor", "White");
    CPanelAnimationVar(Color, m_clrC4Defused, "C4DefusedColor", "White");
};
DECLARE_HUDELEMENT(CHudTeamCounter);

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CHudTeamCounter::CHudTeamCounter(const char *pElementName)
    : CHudElement(pElementName), EditablePanel(NULL, "HudTeamCounter")
{
    vgui::Panel *pParent = g_pClientMode->GetViewport();
    SetParent(pParent);
    SetHiddenBits(HIDEHUD_PLAYERDEAD);

    m_pRoundTimerLabel = new Label(this, "RoundTimerLabel", "0:00");
    m_pCTWinCounterLabel = new Label(this, "CTWinCounterLabel", "0");
    m_pTWinCounterLabel  = new Label(this, "TWinCounterLabel", "0");
    m_pBombIcon = new VectorImagePanel(this, "BombIcon");

    for (int i = 0; i < TC_MAX_AVATAR_SLOTS; i++)
    {
        // CT block
        m_CTSlots[i].pAvatar = new CAvatarImagePanel(this, VarArgs("CT_Av_%d", i));
        m_CTSlots[i].pAvatar->SetShouldScaleImage(true);
        m_CTSlots[i].pAvatar->SetShouldDrawFriendIcon(false);
        m_CTSlots[i].pAvatar->SetVisible(false);
        m_CTSlots[i].pAvatar->SetZPos(2);

        m_CTSlots[i].pSkull = new ImagePanel(this, VarArgs("CT_Sk_%d", i));
        m_CTSlots[i].pSkull->SetImage("hud/teamcounter_aliveskull");
        m_CTSlots[i].pSkull->SetVisible(false);
        m_CTSlots[i].pSkull->SetZPos(6); // skull at top
        {
            KeyValues *kv = new KeyValues("Panel");
            kv->SetInt("scaleImage", 1);
            m_CTSlots[i].pSkull->ApplySettings(kv);
            kv->deleteThis();
        }
        m_CTSlots[i].iLastPlayerIndex = 0;
        m_CTSlots[i].bIsAlive = false;

        m_CTSlots[i].pHPBar = new CHealthBarPanel(this, VarArgs("CT_HPBar_%d", i));
        m_CTSlots[i].pHPBar->SetVisible(false);
        m_CTSlots[i].pHPBar->SetZPos(5); // just under skull

        // T block
        m_TSlots[i].pAvatar = new CAvatarImagePanel(this, VarArgs("T_Av_%d", i));
        m_TSlots[i].pAvatar->SetShouldScaleImage(true);
        m_TSlots[i].pAvatar->SetShouldDrawFriendIcon(false);
        m_TSlots[i].pAvatar->SetVisible(false);
        m_TSlots[i].pAvatar->SetZPos(2);

        m_TSlots[i].pSkull = new ImagePanel(this, VarArgs("T_Sk_%d", i));
        m_TSlots[i].pSkull->SetImage("hud/teamcounter_aliveskull");
        m_TSlots[i].pSkull->SetVisible(false);
        m_TSlots[i].pSkull->SetZPos(6); // skull at top
        {
            KeyValues *kv = new KeyValues("Panel");
            kv->SetInt("scaleImage", 1);
            m_TSlots[i].pSkull->ApplySettings(kv);
            kv->deleteThis();
        }
        m_TSlots[i].iLastPlayerIndex = 0;
        m_TSlots[i].bIsAlive = false;

        m_TSlots[i].pHPBar = new CHealthBarPanel(this, VarArgs("T_HPBar_%d", i));
        m_TSlots[i].pHPBar->SetVisible(false);
        m_TSlots[i].pHPBar->SetZPos(5);

        m_CTSlotRects[i] = { 0,0,0,0 };
        m_TSlotRects[i]  = { 0,0,0,0 };
    }
    m_iAvatarSize = TC_BASE_AVATAR_SIZE;
    m_iOutlineThick = TC_BASE_OUTLINE;
    m_iNumActiveCTSlots = 0;
    m_iNumActiveTSlots = 0;
    LoadControlSettings("resource/hud/teamcounter.res");
}

void CHudTeamCounter::Init(void)
{
    m_iRoundTime = 0;
    m_bIsAtTheBottom = false;
}

void CHudTeamCounter::ApplySchemeSettings(IScheme *pScheme)
{
    BaseClass::ApplySchemeSettings(pScheme);
    IImage *pDefaultCT = vgui::scheme()->GetImage(CSTRIKE_DEFAULT_CT_AVATAR, true);
    IImage *pDefaultT  = vgui::scheme()->GetImage(CSTRIKE_DEFAULT_T_AVATAR, true);

    for (int i = 0; i < TC_MAX_AVATAR_SLOTS; i++)
    {
        if (pDefaultCT) m_CTSlots[i].pAvatar->SetDefaultAvatar(pDefaultCT);
        if (pDefaultT ) m_TSlots[i].pAvatar->SetDefaultAvatar(pDefaultT);
        m_CTSlots[i].pHPBar->SetColors(TC_CT_OUTLINE_COLOR, TC_HP_DELAYED_COLOR, TC_HP_BG_COLOR, true);
        m_TSlots[i].pHPBar->SetColors(TC_T_OUTLINE_COLOR,  TC_HP_DELAYED_COLOR, TC_HP_BG_COLOR, true);

        // Set the skull icon to gray!
        m_CTSlots[i].pSkull->SetDrawColor(TC_DEAD_BG_COLOR);
        m_TSlots[i].pSkull->SetDrawColor(TC_DEAD_BG_COLOR);
    }
}

int CHudTeamCounter::ScalePx(float basePixels) const
{
    float flScale = (float)ScreenHeight() / 1080.0f;
    return MAX(1, int(basePixels * flScale));
}

void CHudTeamCounter::Layout()
{
    m_iAvatarSize   = ScalePx(TC_BASE_AVATAR_SIZE);
    m_iOutlineThick = ScalePx(TC_BASE_OUTLINE);
    int avatarGap   = ScalePx(TC_BASE_AVATAR_GAP);
    int centerW     = ScalePx(TC_BASE_CENTER_W);
    int centerGap   = ScalePx(TC_BASE_CENTER_GAP);
    int hpBarH      = ScalePx(TC_BASE_HP_BAR_HEIGHT);
    int hpBarMargin = ScalePx(TC_BASE_HP_BAR_MARGIN);

    int tileW = m_iAvatarSize + 2 * m_iOutlineThick;
    int tileH = m_iAvatarSize + 2 * m_iOutlineThick;
    int numRows = (TC_MAX_AVATAR_SLOTS + TC_AVATARS_PER_ROW - 1) / TC_AVATARS_PER_ROW;
    int teamBlockW = TC_AVATARS_PER_ROW * tileW + (TC_AVATARS_PER_ROW - 1) * avatarGap;
    int teamBlockH = numRows * tileH + (numRows - 1) * avatarGap;
    int panelW = teamBlockW + centerGap + centerW + centerGap + teamBlockW;
    int panelH = teamBlockH;

    int panelX = (ScreenWidth() - panelW) / 2;
    int panelY = m_bIsAtTheBottom ? ScreenHeight() - panelH - ScalePx(4) : ScalePx(2);
    
    SetPos(panelX, panelY);
    SetSize(panelW, panelH);

    int ctOriginX = 0;
    for (int i = 0; i < TC_MAX_AVATAR_SLOTS; i++)
    {
        int row = i % 2, col = i / 2; col = (TC_AVATARS_PER_ROW - 1) - col;
        int sx = ctOriginX + col * (tileW + avatarGap);
        int sy = row * (tileH + avatarGap);
        m_CTSlotRects[i] = { sx, sy, tileW, tileH };
        int ax = sx + m_iOutlineThick, ay = sy + m_iOutlineThick;
        m_CTSlots[i].pAvatar->SetBounds(ax, ay, m_iAvatarSize, m_iAvatarSize);
        m_CTSlots[i].pAvatar->SetAvatarSize(m_iAvatarSize, m_iAvatarSize);
        m_CTSlots[i].pSkull->SetBounds(ax, ay, m_iAvatarSize, m_iAvatarSize);

        // HEATHBAR: inside avatar, bottom, with margin
m_CTSlots[i].pHPBar->SetBounds(
    ax,
    ay + m_iAvatarSize - ScalePx(TC_BASE_HP_BAR_HEIGHT),
    m_iAvatarSize,
    ScalePx(TC_BASE_HP_BAR_HEIGHT)
);
    }
    int centerOriginX = teamBlockW + centerGap;
    int tOriginX = centerOriginX + centerW + centerGap;
    for (int i = 0; i < TC_MAX_AVATAR_SLOTS; i++)
    {
        int row = i % 2, col = i / 2;
        int sx = tOriginX + col * (tileW + avatarGap), sy = row * (tileH + avatarGap);
        m_TSlotRects[i] = { sx, sy, tileW, tileH };
        int ax = sx + m_iOutlineThick, ay = sy + m_iOutlineThick;
        m_TSlots[i].pAvatar->SetBounds(ax, ay, m_iAvatarSize, m_iAvatarSize);
        m_TSlots[i].pAvatar->SetAvatarSize(m_iAvatarSize, m_iAvatarSize);
        m_TSlots[i].pSkull->SetBounds(ax, ay, m_iAvatarSize, m_iAvatarSize);

        // HEATHBAR: inside avatar, bottom
m_TSlots[i].pHPBar->SetBounds(
    ax,
    ay + m_iAvatarSize - ScalePx(TC_BASE_HP_BAR_HEIGHT),
    m_iAvatarSize,
    ScalePx(TC_BASE_HP_BAR_HEIGHT)
);
    }
}

void CHudTeamCounter::PerformLayout()
{
    BaseClass::PerformLayout();
    Layout();
}

void CHudTeamCounter::PaintBackground()
{
    BaseClass::PaintBackground();
    vgui::ISurface *pSurface = vgui::surface();
    if (!pSurface) return;

    for (int i = 0; i < m_iNumActiveCTSlots; i++)
    {
        if (!m_CTSlots[i].bIsAlive) continue;
        const auto &r = m_CTSlotRects[i];
        pSurface->DrawSetColor(TC_CT_OUTLINE_COLOR);
        pSurface->DrawFilledRect(r.x, r.y, r.x + r.w, r.y + r.h);
    }
    for (int i = 0; i < m_iNumActiveTSlots; i++)
    {
        if (!m_TSlots[i].bIsAlive) continue;
        const auto &r = m_TSlotRects[i];
        pSurface->DrawSetColor(TC_T_OUTLINE_COLOR);
        pSurface->DrawFilledRect(r.x, r.y, r.x + r.w, r.y + r.h);
    }
}

void CHudTeamCounter::Reset()
{
    g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("RoundTimerReset");
}

bool CHudTeamCounter::ShouldDraw()
{
    C_CSPlayer *pPlayer = C_CSPlayer::GetLocalCSPlayer();
    if (!pPlayer) return false;
    if (pPlayer->IsObserver()) return false;
    return true;
}

void CHudTeamCounter::UpdateAvatarMode()
{
    if (!g_PR) return;
    CUtlVector<int> ctList, tList;
    ctList.EnsureCapacity(TC_MAX_AVATAR_SLOTS);
    tList.EnsureCapacity(TC_MAX_AVATAR_SLOTS);
    for (int i = 1; i <= MAX_PLAYERS; i++)
    {
        if (!g_PR->IsConnected(i)) continue;
        int team = g_PR->GetTeam(i);
        if (team == TEAM_CT && ctList.Count() < TC_MAX_AVATAR_SLOTS)
            ctList.AddToTail(i);
        else if (team == TEAM_TERRORIST && tList.Count() < TC_MAX_AVATAR_SLOTS)
            tList.AddToTail(i);
    }
    m_iNumActiveCTSlots = ctList.Count();
    m_iNumActiveTSlots  = tList.Count();
    for (int slot = 0; slot < TC_MAX_AVATAR_SLOTS; slot++)
    {
        if (slot < ctList.Count())
        {
            int playerIndex = ctList[slot];
            bool bAlive = g_PR->IsAlive(playerIndex);
            if (m_CTSlots[slot].iLastPlayerIndex != playerIndex)
            {
                m_CTSlots[slot].iLastPlayerIndex = playerIndex;
                m_CTSlots[slot].pAvatar->SetPlayer(playerIndex, k_EAvatarSize32x32);
            }
            m_CTSlots[slot].bIsAlive = bAlive;
            m_CTSlots[slot].pAvatar->SetVisible(bAlive);
            m_CTSlots[slot].pSkull->SetVisible(!bAlive);
            m_CTSlots[slot].pSkull->SetDrawColor(TC_DEAD_BG_COLOR);
        }
        else
        {
            m_CTSlots[slot].iLastPlayerIndex = 0;
            m_CTSlots[slot].bIsAlive = false;
            m_CTSlots[slot].pAvatar->SetVisible(false);
            m_CTSlots[slot].pSkull->SetVisible(false);
        }
        if (slot < tList.Count())
        {
            int playerIndex = tList[slot];
            bool bAlive = g_PR->IsAlive(playerIndex);
            if (m_TSlots[slot].iLastPlayerIndex != playerIndex)
            {
                m_TSlots[slot].iLastPlayerIndex = playerIndex;
                m_TSlots[slot].pAvatar->SetPlayer(playerIndex, k_EAvatarSize32x32);
            }
            m_TSlots[slot].bIsAlive = bAlive;
            m_TSlots[slot].pAvatar->SetVisible(bAlive);
            m_TSlots[slot].pSkull->SetVisible(!bAlive);
            m_TSlots[slot].pSkull->SetDrawColor(TC_DEAD_BG_COLOR);
        }
        else
        {
            m_TSlots[slot].iLastPlayerIndex = 0;
            m_TSlots[slot].bIsAlive = false;
            m_TSlots[slot].pAvatar->SetVisible(false);
            m_TSlots[slot].pSkull->SetVisible(false);
        }
    }
}

void CHudTeamCounter::OnThink()
{
    if (m_bIsAtTheBottom != hud_playercount_pos.GetBool())
    {
        m_bIsAtTheBottom = hud_playercount_pos.GetBool();
        InvalidateLayout(true, false);
    }
    wchar_t unicode[8];
    C_CSTeam *teamCT = static_cast<C_CSTeam*>(GetGlobalTeam(TEAM_CT));
    C_CSTeam *teamT = static_cast<C_CSTeam*>(GetGlobalTeam(TEAM_TERRORIST));
    if (teamCT)
    {
        V_snwprintf(unicode, ARRAYSIZE(unicode), L"%d", teamCT->Get_Score());
        m_pCTWinCounterLabel->SetText(unicode);
    }
    if (teamT)
    {
        V_snwprintf(unicode, ARRAYSIZE(unicode), L"%d", teamT->Get_Score());
        m_pTWinCounterLabel->SetText(unicode);
    }
    UpdateAvatarMode();

    float frameTime = gpGlobals->frametime;
    int localTeam = C_CSPlayer::GetLocalCSPlayer()? C_CSPlayer::GetLocalCSPlayer()->GetTeamNumber() : TEAM_UNASSIGNED;
    for (int slot = 0; slot < TC_MAX_AVATAR_SLOTS; slot++)
    {
        // -- CT --
        bool alive_CT = (slot < m_iNumActiveCTSlots) && m_CTSlots[slot].bIsAlive;
        bool show_CT = (localTeam == TEAM_CT) && alive_CT;
        m_CTSlots[slot].pHPBar->SetVisible(show_CT);
        if (show_CT)
        {
            int playerIndex = m_CTSlots[slot].iLastPlayerIndex;
            int hp = (g_PR && playerIndex > 0) ? g_PR->GetHealth(playerIndex) : 0;
            float frac = clamp(hp / 100.0f, 0.f, 1.f);
            m_CTSlots[slot].pHPBar->SetHealthFraction(frac);
            Color hpColor = TC_CT_OUTLINE_COLOR;

if (hp <= 20)
{
    hpColor = TC_HP_LOW_COLOR;
}

m_CTSlots[slot].pHPBar->SetColors(
    hpColor,
    TC_HP_DELAYED_COLOR,
    TC_HP_BG_COLOR,
    true);
            m_CTSlots[slot].pHPBar->TickAnim(frameTime);
        } else {
            m_CTSlots[slot].pHPBar->SetVisible(false);
        }
        // -- T --
        bool alive_T = (slot < m_iNumActiveTSlots) && m_TSlots[slot].bIsAlive;
        bool show_T = (localTeam == TEAM_TERRORIST) && alive_T;
        m_TSlots[slot].pHPBar->SetVisible(show_T);
        if (show_T)
        {
            int playerIndex = m_TSlots[slot].iLastPlayerIndex;
            int hp = (g_PR && playerIndex > 0) ? g_PR->GetHealth(playerIndex) : 0;
            float frac = clamp(hp / 100.0f, 0.f, 1.f);
            m_TSlots[slot].pHPBar->SetHealthFraction(frac);
            Color hpColor = TC_T_OUTLINE_COLOR;

if (hp <= 20)
{
    hpColor = TC_HP_LOW_COLOR;
}

m_TSlots[slot].pHPBar->SetColors(
    hpColor,
    TC_HP_DELAYED_COLOR,
    TC_HP_BG_COLOR,
    true);
            m_TSlots[slot].pHPBar->TickAnim(frameTime);
        } else {
            m_TSlots[slot].pHPBar->SetVisible(false);
        }
    }
            C_CSGameRules *pRules = CSGameRules();
        if ( !pRules )
                return;

        // Check if bomb is planted
        bool bBombPlanted = ( g_PlantedC4s.Count() > 0 );

        if ( bBombPlanted )
        {
                C_PlantedC4 *pC4 = g_PlantedC4s[0];

                // Check if defused
                if ( pC4->m_bBombDefused )
                {
                        // Bomb defused - show solid green icon
                        m_pBombIcon->SetAlpha( 255 );
                        m_pBombIcon->SetFgColor( m_clrC4Defused );
                        m_pBombIcon->SetVisible( true );
                }
                else
                {
                        // Bomb still ticking - pulsing effect based on m_flNextGlow
                        int alpha = 255;
                        if ( gpGlobals->curtime + 0.1f >= pC4->m_flNextGlow )
                                alpha = 128;  // Dim when not glowing

                        m_pBombIcon->SetAlpha( alpha );
                        m_pBombIcon->SetFgColor( m_clrC4Planted );

                        // Hide bomb icon when explode warning is active
                        m_pBombIcon->SetVisible( !pC4->m_bExplodeWarning );
                }
        }
        else
        {
                m_pBombIcon->SetVisible( false );
        }

        // Timer text - empty when bomb planted, time out active, or warmup
        if ( bBombPlanted || pRules->IsTimeOutActive() || pRules->IsWarmupPeriod() )
        {
                // Show empty space (like original script)
                m_pRoundTimerLabel->SetText( L" " );
        }
        else
        {
                // Normal timer logic
                if ( m_iRoundTime < (int)ceil( pRules->GetRoundRemainingTime() ) )
                        g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "RoundTimerReset" );

                m_iRoundTime = (int)ceil( pRules->GetRoundRemainingTime() );

                if ( pRules->IsFreezePeriod() )
                {
                        // In freeze period, countdown to round start time
                        m_iRoundTime = (int)ceil( pRules->GetRoundStartTime() - gpGlobals->curtime );
                }

                if ( m_iRoundTime < 0 )
                        m_iRoundTime = 0;

                if ( m_iRoundTime <= 10 )
                        g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "RoundTimerLow" );

                int iMinutes = m_iRoundTime / 60;
                int iSeconds = m_iRoundTime % 60;

                V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d : %.2d", iMinutes, iSeconds );
                m_pRoundTimerLabel->SetText( unicode );
        }
}