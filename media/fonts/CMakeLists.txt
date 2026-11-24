
list(APPEND FONT_FILES
    arial.ttf
    arialbd.ttf
    arialbi.ttf
    ariali.ttf
    ariblk.ttf
    comic.ttf
    comicbd.ttf
    cour.ttf
    courbd.ttf
    courbi.ttf
    couri.ttf
    framd.ttf
    framdit.ttf
    FreeSans.ttf
    FreeSansBold.ttf
    FreeSansBoldOblique.ttf
    FreeSansOblique.ttf
    FSEX301.ttf
    FSEX301-L2.ttf
    georgia.ttf
    georgiab.ttf
    georgiai.ttf
    georgiaz.ttf
    lucon.ttf
    Marlett.ttf
    micross.ttf
    NotoSans-Bold.ttf
    NotoSans-BoldItalic.ttf
    NotoSans-Condensed-Bold.ttf
    NotoSans-Condensed-BoldItalic.ttf
    NotoSans-Condensed-Italic.ttf
    NotoSans-Condensed-Light.ttf
    NotoSans-Condensed-LightItalic.ttf
    NotoSans-Condensed-Regular.ttf
    NotoSans-Condensed-Thin.ttf
    NotoSans-Condensed-ThinItalic.ttf
    NotoSans-ExtraCondensed-Bold.ttf
    NotoSans-ExtraCondensed-BoldItalic.ttf
    NotoSans-ExtraCondensed-Italic.ttf
    NotoSans-ExtraCondensed-Light.ttf
    NotoSans-ExtraCondensed-LightItalic.ttf
    NotoSans-ExtraCondensed-Regular.ttf
    NotoSans-ExtraCondensed-Thin.ttf
    NotoSans-ExtraCondensed-ThinItalic.ttf
    NotoSans-Italic.ttf
    NotoSans-Light.ttf
    NotoSans-LightItalic.ttf
    NotoSans-Regular.ttf
    NotoSans-SemiCondensed-Bold.ttf
    NotoSans-SemiCondensed-BoldItalic.ttf
    NotoSans-SemiCondensed-Italic.ttf
    NotoSans-SemiCondensed-Light.ttf
    NotoSans-SemiCondensed-LightItalic.ttf
    NotoSans-SemiCondensed-Regular.ttf
    NotoSans-SemiCondensed-Thin.ttf
    NotoSans-SemiCondensed-ThinItalic.ttf
    NotoSans-Thin.ttf
    NotoSans-ThinItalic.ttf
    NotoSansThai-Bold.ttf
    NotoSansThai-Condensed-Bold.ttf
    NotoSansThai-Condensed-Light.ttf
    NotoSansThai-Condensed-Regular.ttf
    NotoSansThai-Condensed-Thin.ttf
    NotoSansThai-ExtraCondensed-Bold.ttf
    NotoSansThai-ExtraCondensed-Light.ttf
    NotoSansThai-ExtraCondensed-Regular.ttf
    NotoSansThai-ExtraCondensed-Thin.ttf
    NotoSansThai-Light.ttf
    NotoSansThai-Regular.ttf
    NotoSansThai-SemiCondensed-Bold.ttf
    NotoSansThai-SemiCondensed-Light.ttf
    NotoSansThai-SemiCondensed-Regular.ttf
    NotoSansThai-SemiCondensed-Thin.ttf
    NotoSansThai-Thin.ttf
    pala.ttf
    palab.ttf
    palabi.ttf
    palai.ttf
    SourceSansPro-Regular.ttf
    SourceSansPro-Bold.ttf
    SourceSansPro-BoldIt.ttf
    SourceSansPro-It.ttf
    #ssee874.fon # to be added (CORE-16165 and CORE-16166)
    #ssee1255.fon # to be added (CORE-16165 and CORE-16166)
    #ssee1256.fon # to be added (CORE-16165 and CORE-16166)
    #ssee1257.fon # to be added (CORE-16165 and CORE-16166)
    #sserife.fon # to be added (CORE-16165 and CORE-16166)
    #sserifeg.fon # to be added (CORE-16165 and CORE-16166)
    #sserifer.fon # to be added (CORE-16165 and CORE-16166)
    #sserifet.fon # to be added (CORE-16165 and CORE-16166)
    selawk.ttf
    selawkb.ttf
    selawkl.ttf
    selawksb.ttf
    selawksl.ttf
    symbol.ttf
    tahoma.ttf
    tahomabd.ttf
    times.ttf
    timesbd.ttf
    timesbi.ttf
    timesi.ttf
    trebuc.ttf
    trebucbd.ttf
    trebucbi.ttf
    trebucit.ttf
    UniVGA16.ttf
    verdanab.ttf
    verdanaz.ttf
    verdanai.ttf
    verdana.ttf
    #vgaoem.fon # to be added (CORE-17327)
    wingding.ttf)

foreach(item ${FONT_FILES})
    add_cd_file(FILE "${CMAKE_CURRENT_SOURCE_DIR}/${item}" DESTINATION reactos/Fonts FOR all)
endforeach(item)
