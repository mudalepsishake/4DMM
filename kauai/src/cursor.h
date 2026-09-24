/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Cursor class.

***************************************************************************/
#ifndef CURSOR_H
#define CURSOR_H

enum
{
    curtMonochrome = 0,
};

// 3DMMv1.0: cursor on file - stored in a GG with the rgb's in the variable part
struct CURF
{
    int32_t curt; // 3DMMv1.0: type of cursor
    uint8_t xp;   // 3DMMv1.0: hot spot
    uint8_t yp;
    uint8_t dxp; // 3DMMv1.0: size - either 16 or 32 and they should match
    uint8_t dyp;
    // 3DMMEx: uint8_t rgbAnd[];
    // 3DMMEx: uint8_t rgbXor[];
};
VERIFY_STRUCT_SIZE(CURF, 8);
const BOM kbomCurf = 0xC0000000;

typedef class CURS *PCURS;
#define CURS_PAR BACO
#define kclsCURS KLCONST4('C', 'U', 'R', 'S')
class CURS : public CURS_PAR
{
    RTCLASS_DEC

  private:
  protected:
#ifdef KAUAI_WIN32
    HCRS _hcrs;
#endif // 3DMMEx: KAUAI_WIN32
#ifdef MAC
    Cursor _crs;
#endif // 3DMMv1.0: MAC
#ifdef KAUAI_SDL
    SDL_Cursor *_crs;
#endif // 3DMMEx: KAUAI_SDL

    CURS(void)
    {
    } // 3DMMv1.0: we have to be allocated
    ~CURS(void);

  public:
    static bool FReadCurs(PCRF pcrf, CTG ctg, CNO cno, BLCK *pblck, PBACO *ppbaco, int32_t *pcb);

    void Set(void);
};

#endif //! 3DMMv1.0: CURSOR_H
