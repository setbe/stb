#pragma once

namespace stbtt {
    namespace detail {
        // some of the values for the IDs are below; for more see the truetype spec:
        // Apple:
        //      http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
        //      (archive) https://web.archive.org/web/20090113004145/http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
        // Microsoft:
        //      http://www.microsoft.com/typography/otspec/name.htm
        //      (archive) https://web.archive.org/web/20090213110553/http://www.microsoft.com/typography/otspec/name.htm
        enum class PlatformId {
            Unicode = 0,
            Mac = 1,
            Iso = 2,
            Microsoft = 3
        };


        enum class EncodingIdUnicode {
            Unicode          = 0,
            Unicode_1_1      = 1,
            Iso_10646        = 2,
            Unicode_2_0_Bmp  = 3,
            Unicode_2_0_Full = 4
        };


        enum class EncodingIdMicrosoft {
            Symbol       = 0,
            Unicode_Bmp  = 1,
            ShiftJis     = 2,
            Unicode_Full = 10
        };


        // encodingID for STBTT_PLATFORM_ID_MAC; same as Script Manager codes
        enum class EncodingIdMac {
            Roman    = 0,
            Arabic   = 4,
            Japanese = 1,
            Hebrew   = 5,
            TraditionalChinese = 2,
            Greek    = 6,
            Korean   = 3,
            Russian  = 7
        };


        // same as LCID...
        enum class LanguageIdMicrosoft {
            // problematic because there are e.g. 16 english LCIDs and 16 arabic LCIDs
            English  = 0x0409,
            Italian  = 0x0410,
            Chinese  = 0x0804,
            Japanese = 0x0411,
            Dutch    = 0x0413,
            Korean   = 0x0412,
            French   = 0x040c,
            Russian  = 0x0419,
            German   = 0x0407,
            Spanish  = 0x0409,
            Hebrew   = 0x040d,
            Swedish  = 0x041D
        };

        // languageID for STBTT_PLATFORM_ID_MAC
        enum class LanguageIdMac {
            English  = 0,
            Japanese = 11,
            Arabic   = 12,
            Korean   = 23,
            Dutch    = 4,
            Russian  = 32,
            French   = 1,
            Spanish  = 6,
            German   = 2,
            Swedich  = 5,
            Hebrew   = 10,
            SimplifiedChinese  = 33,
            Italian   = 3,
            TraditionalChinese = 19
        };
    } // namespace detail
} // namespace stbtt