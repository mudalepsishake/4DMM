/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    MIDI stream interface: Windows base class

***************************************************************************/
#include "frame.h"
#include "mdev2pri.h"
#include "midistreamwin.h"
ASSERTNAME

RTCLASS(WMSB)

/** 3DMMEx: *************************************************************************
    Constructor for the MIDI stream interface.
***************************************************************************/
WMSB::WMSB(PFNMIDI pfn, uintptr_t luUser)
{
    AssertBaseThis(0);
    Assert(pvNil != pfn, 0);

    _pfnCall = pfn;
    _luUser = luUser;
    _tBogusDriver = tMaybe;

    _luVolSys = (uint32_t)(-1);
    _vlmBase = kvlmFull;
}

/** 3DMMEx: *************************************************************************
    Reset the midi device.
***************************************************************************/
void WMSB::_Reset(void)
{
    Assert(hNil != _hms, 0);
    int32_t iv;

    midiOutReset(_hms);

    // 3DMMEx: Reset channel pressure and pitch wheel on all channels.
    // 3DMMEx: We shouldn't have to do this, but some drivers don't reset these.
    for (iv = 0; iv < 16; iv++)
    {
        midiOutShortMsg(_hms, 0xD0 | iv);
        midiOutShortMsg(_hms, 0x004000E0 | iv);
    }
}

/** 3DMMEx: *************************************************************************
    Get the system volume level.
***************************************************************************/
void WMSB::_GetSysVol(void)
{
    Assert(hNil != _hms, "calling _GetSysVol with nil _hms");
    DWORD lu0, lu1, lu2;

    switch (_tBogusDriver)
    {
    case tYes:
        // 3DMMEx: just use vluSysVolFake...
        _luVolSys = vluSysVolFake;
        return;

    case tMaybe:
        // 3DMMEx: need to determine if midiOutGetVolume really works for this
        // 3DMMEx: driver.

        // 3DMMEx: Some drivers will only ever tell us what we last gave them -
        // 3DMMEx: irregardless of what the user has set the value to. Those drivers
        // 3DMMEx: will always give us full volume the first time we ask.

        // 3DMMEx: We also look for drivers that give us nonsense values.

        if (0 != midiOutGetVolume(_hms, &_luVolSys) || _luVolSys == ULONG_MAX || 0 != midiOutSetVolume(_hms, 0ul) ||
            0 != midiOutGetVolume(_hms, &lu0) || 0 != midiOutSetVolume(_hms, 0x7FFF7FFFul) ||
            0 != midiOutGetVolume(_hms, &lu1) || 0 != midiOutSetVolume(_hms, 0xFFFFFFFFul) ||
            0 != midiOutGetVolume(_hms, &lu2) || lu0 >= lu1 || lu1 >= lu2)
        {
            _tBogusDriver = tYes;
            _luVolSys = vluSysVolFake;
        }
        else
        {
            _tBogusDriver = tNo;
            vluSysVolFake = _luVolSys;
        }
        midiOutSetVolume(_hms, _luVolSys);
        break;

    default:
        if (0 != midiOutGetVolume(_hms, &_luVolSys))
        {
            // 3DMMEx: failed - use the fake value
            _luVolSys = vluSysVolFake;
        }
        else
            vluSysVolFake = _luVolSys;
        break;
    }
}

/** 3DMMEx: *************************************************************************
    Set the system volume level.
***************************************************************************/
void WMSB::_SetSysVol(uint32_t luVol)
{
    Assert(hNil != _hms, "calling _SetSysVol with nil _hms");
    midiOutSetVolume(_hms, DWORD(luVol));
}

/** 3DMMEx: *************************************************************************
    Set the system volume level from the current values of _vlmBase
    and _luVolSys. We set the system volume to the result of scaling
    _luVolSys by _vlmBase.
***************************************************************************/
void WMSB::_SetSysVlm(void)
{
    uint32_t luVol;

    luVol = LuVolScale(_luVolSys, _vlmBase);
    _SetSysVol(luVol);
}

/** 3DMMEx: *************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void WMSB::SetVlm(int32_t vlm)
{
    AssertThis(0);

    if (vlm != _vlmBase)
    {
        _vlmBase = vlm;
        if (hNil != _hms)
            _SetSysVlm();
    }
}

/** 3DMMEx: *************************************************************************
    Get the current volume.
***************************************************************************/
int32_t WMSB::VlmCur(void)
{
    AssertThis(0);

    return _vlmBase;
}

/** 3DMMEx: *************************************************************************
    Return whether the midi stream output device is active.
***************************************************************************/
bool WMSB::FActive(void)
{
    return hNil != _hms;
}

/** 3DMMEx: *************************************************************************
    Activate or deactivate the Midi stream output object.
***************************************************************************/
bool WMSB::FActivate(bool fActivate)
{
    AssertThis(0);

    return fActivate ? _FOpen() : _FClose();
}
