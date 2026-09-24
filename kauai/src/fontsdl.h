/** 3DMMEx: *************************************************************************
    Author: Ben Stone
    Project: Kauai
    Reviewed:

    SDL Font management.

***************************************************************************/

#ifndef FONTSDL_H
#define FONTSDL_H

// 3DMMEx: Bitmask of all supported font styles
#define fontAll (fontBold | fontItalic | fontUnderline | fontBoxed)

const FTG kftgTtf = KLCONST3('t', 't', 'f');
const FTG kftgTtc = KLCONST3('t', 't', 'c');
const FTG kftgOtf = KLCONST3('o', 't', 'f');

typedef class SDLFont *PSDLFont;

#define SDLFont_PAR BASE
#define kclsSDLFont KLCONST4('s', 'f', 'n', 't')

// 3DMMEx: Abstract SDL font
class SDLFont : public SDLFont_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(SDLFont)

  public:
    virtual ~SDLFont() override;

    /** 3DMMEx:
     * @brief Get the SDL_TTF font object. This will load the font if not already loaded.
     * @param dyp       Font height in points
     **/
    TTF_Font *PttfFont(int32_t dyp);

    /** 3DMMEx:
     * @brief Return bitmask for supported font styles
     * @return
     */
    int32_t Grfont()
    {
        return _grfont;
    }

  protected:
    // 3DMMEx: fTrue if we failed to load the font
    bool _fLoadFailed = fFalse;

    // 3DMMEx: Font style flags
    int32_t _grfont = 0;

    // 3DMMEx: Return a SDL_RWops object for reading font data
    virtual SDL_RWops *GetFontRWops() = 0;

    struct Instance
    {
        int32_t dyp;        // 3DMMEx: Font height in points
        TTF_Font *pttffont; // 3DMMEx: Pointer to loaded font
    };

    // 3DMMEx: List of font instances
    PGL _pglinstance = pvNil;
};

typedef class SDLFontFile *PSDLFontFile;

#define SDLFontFile_PAR SDLFont
#define kclsSDLFontFile KLCONST4('f', 'n', 't', 'f')

// 3DMMEx: SDL Font File
class SDLFontFile : public SDLFontFile_PAR
{
    RTCLASS_DEC
    NOCOPY(SDLFontFile)

  public:
    /** 3DMMEx:
     * @brief Create a new font file object
     *
     * @param fniFont Path to font file
     * @param grffont Font style flags
     * @return SDLFontFile object represented the font
     */
    static PSDLFontFile PSDLFontFileNew(PFNI pfniFont, int32_t grffont);

  private:
    // 3DMMEx: Path to font file
    FNI _fniFont;

    SDL_RWops *GetFontRWops() override;
};

typedef class SDLFontMemory *PSDLFontMemory;

#define SDLFontMemory_PAR SDLFont
#define kclsSDLFontMemory KLCONST4('f', 'n', 't', 'm')

// 3DMMEx: SDL Font from memory
class SDLFontMemory : public SDLFontMemory_PAR
{
    RTCLASS_DEC
    NOCOPY(SDLFontMemory)

  public:
    /** 3DMMEx:
     * @brief Create a new font from memory
     *
     * @param pbFont Font data
     * @param cbFont Size of font data
     * @param grffont Font style flags
     * @return SDLFontMemory object represented the loaded font
     */
    static PSDLFontMemory PSDLFontMemoryNew(const uint8_t *pbFont, const int32_t cbFont, int32_t grffont);

  private:
    const uint8_t *_pbFont = pvNil;
    int32_t _cbFont = 0;

    SDL_RWops *GetFontRWops() override;
};
#endif // 3DMMEx: FONTSDL_H
