                AREA    GREENMIND_FONT_DATA, DATA, READONLY, ALIGN=2

                EXPORT  g_greenmind_gb2312_font
                EXPORT  g_greenmind_gb2312_font_end
                EXPORT  g_greenmind_unicode_gb2312_map
                EXPORT  g_greenmind_unicode_gb2312_map_end

g_greenmind_gb2312_font
                INCBIN  Assets\fonts\greenmind_gb2312_12x12.bin
g_greenmind_gb2312_font_end

                ALIGN   2
g_greenmind_unicode_gb2312_map
                INCBIN  Assets\fonts\greenmind_unicode_to_gb2312.bin
g_greenmind_unicode_gb2312_map_end

                END
