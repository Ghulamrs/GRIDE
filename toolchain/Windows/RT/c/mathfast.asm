; mathfast.asm - exp, log, sin, cos and pow (grtc): the double kernels a program's time goes to, in assembly
; (docs\Runtime_Design.md 10.4). Written by tools\mathfast.py with its tables; do not edit - change the script.
;
; Spec: ISO C 7.12.6.1 exp, 7.12.6.7 log, 7.12.4.6 sin, 7.12.4.5 cos, 7.12.7.4 pow, within an ulp of the true
; value and of the UCRT's (tests\math99, the UCRT probes). Each takes the common case here and leaves everything
; else - a NaN, an infinity, a zero, a subnormal, a result near overflow or underflow, a very large argument of sin
; and cos, pow with x not positive or y outside 2^-60..2^60 - to RTS6x's kernels (c\mathslow.cpp), which give the
; special values and errno. Only xmm0-xmm5 are used, so no register needs saving.
;   exp  x = (128 n + j) ln2/128 + r, the step's product subtracted in two parts so the first is exact; e^r - 1 by
;        its Taylor series to r^5 (|r| <= ln2/256); 2^(j/128) as hi + lo; the exponent added into the bits
;   log  z = x 2^-k in [0.707, 1.414); z invc - 1 exact in two parts (z's top 45 bits times the 8-bit invc, then
;        the rest); ln x = k ln2 + ln(1/invc) + log1p(r), the large terms summed with their errors, log1p by its
;        Taylor series to r^8 (r^9 for pow) in two chains of r^2; k's register cleared before cvtsi2sd, which
;        would otherwise wait for the register's last writer, one call's tail holding up the next
;   sin  x = n pi/2 + r by pi/2 in three parts, r kept with its low part; sin r or cos r by the quadrant, each by
;   cos  its Taylor series in two chains of r^4; cos as 1 - r^2/2 with that subtraction's error kept
;   pow  ln x as a double-double from log's kernel, y ln x as one by Dekker's product, e^ of it by exp's kernel
; Where the processor has the fused multiply-add (__grt_fma, c\memory.asm), each takes it: log's r = z invc - 1 in
; one exact step, the polynomials' steps fused, pow's y ln x error by fma(y, hi, -product). The results are those of
; the other path but in the last bit of rare cases. GRIDE's masm has no FMA mnemonics: they are written as bytes.

PUBLIC exp
PUBLIC log
PUBLIC sin
PUBLIC cos
PUBLIC pow
EXTERN __grt_exp_slow:PROC
EXTERN __grt_log_slow:PROC
EXTERN __grt_sin_slow:PROC
EXTERN __grt_cos_slow:PROC
EXTERN __grt_pow_slow:PROC
EXTERN __grt_fma:DWORD

_DATA SEGMENT
expScale DQ 040671547652B82FEH
expStepHi DQ 03F762E42FF000000H
expStepLo DQ 0BD5718432A1B0E26H
expC2 DQ 03FE0000000000000H
expC3 DQ 03FC5555555555555H
expC4 DQ 03FA5555555555555H
expC5 DQ 03F81111111111111H
expTable DQ 03FF0000000000000H, 00000000000000000H
  DQ 03FF0163DA9FB3335H, 03C9B61299AB8CDB7H
  DQ 03FF02C9A3E778061H, 0BC719083535B085DH
  DQ 03FF04315E86E7F85H, 0BC90A31C1977C96EH
  DQ 03FF059B0D3158574H, 03C8D73E2A475B465H
  DQ 03FF0706B29DDF6DEH, 0BC8C91DFE2B13C27H
  DQ 03FF0874518759BC8H, 03C6186BE4BB284FFH
  DQ 03FF09E3ECAC6F383H, 03C91487818316136H
  DQ 03FF0B5586CF9890FH, 03C98A62E4ADC610BH
  DQ 03FF0CC922B7247F7H, 03C901EDC16E24F71H
  DQ 03FF0E3EC32D3D1A2H, 03C403A1727C57B53H
  DQ 03FF0FB66AFFED31BH, 0BC6B9BEDC44EBD7BH
  DQ 03FF11301D0125B51H, 0BC96C51039449B3AH
  DQ 03FF12ABDC06C31CCH, 0BC51B514B36CA5C7H
  DQ 03FF1429AAEA92DE0H, 0BC932FBF9AF1369EH
  DQ 03FF15A98C8A58E51H, 03C82406AB9EEAB0AH
  DQ 03FF172B83C7D517BH, 0BC819041B9D78A76H
  DQ 03FF18AF9388C8DEAH, 0BC911023D1970F6CH
  DQ 03FF1A35BEB6FCB75H, 03C8E5B4C7B4968E4H
  DQ 03FF1BBE084045CD4H, 0BC995386352EF607H
  DQ 03FF1D4873168B9AAH, 03C9E016E00A2643CH
  DQ 03FF1ED5022FCD91DH, 0BC91DF98027BB78CH
  DQ 03FF2063B88628CD6H, 03C8DC775814A8495H
  DQ 03FF21F49917DDC96H, 03C82A97E9494A5EEH
  DQ 03FF2387A6E756238H, 03C99B07EB6C70573H
  DQ 03FF251CE4FB2A63FH, 03C8AC155BEF4F4A4H
  DQ 03FF26B4565E27CDDH, 03C82BD339940E9D9H
  DQ 03FF284DFE1F56381H, 0BC9A4C3A8C3F0D7EH
  DQ 03FF29E9DF51FDEE1H, 03C8612E8AFAD1255H
  DQ 03FF2B87FD0DAD990H, 0BC410ADCD6381AA4H
  DQ 03FF2D285A6E4030BH, 03C90024754DB41D5H
  DQ 03FF2ECAFA93E2F56H, 03C71CA0F45D52383H
  DQ 03FF306FE0A31B715H, 03C86F46AD23182E4H
  DQ 03FF32170FC4CD831H, 03C8A9CE78E18047CH
  DQ 03FF33C08B26416FFH, 03C932721843659A6H
  DQ 03FF356C55F929FF1H, 0BC8B5CEE5C4E4628H
  DQ 03FF371A7373AA9CBH, 0BC963AEABF42EAE2H
  DQ 03FF38CAE6D05D866H, 0BC9E958D3C9904BDH
  DQ 03FF3A7DB34E59FF7H, 0BC75E436D661F5E3H
  DQ 03FF3C32DC313A8E5H, 0BC9EFFF8375D29C3H
  DQ 03FF3DEA64C123422H, 03C8ADA0911F09EBCH
  DQ 03FF3FA4504AC801CH, 0BC97D023F956F9F3H
  DQ 03FF4160A21F72E2AH, 0BC5EF3691C309278H
  DQ 03FF431F5D950A897H, 0BC81C7DDE35F7999H
  DQ 03FF44E086061892DH, 03C489B7A04EF80D0H
  DQ 03FF46A41ED1D0057H, 03C9C944BD1648A76H
  DQ 03FF486A2B5C13CD0H, 03C73C1A3B69062F0H
  DQ 03FF4A32AF0D7D3DEH, 03C99CB62F3D1BE56H
  DQ 03FF4BFDAD5362A27H, 03C7D4397AFEC42E2H
  DQ 03FF4DCB299FDDD0DH, 03C98ECDBBC6A7833H
  DQ 03FF4F9B2769D2CA7H, 0BC94B309D25957E3H
  DQ 03FF516DAA2CF6642H, 0BC8F768569BD93EFH
  DQ 03FF5342B569D4F82H, 0BC807ABE1DB13CADH
  DQ 03FF551A4CA5D920FH, 0BC8D689CEFEDE59BH
  DQ 03FF56F4736B527DAH, 03C99BB2C011D93ADH
  DQ 03FF58D12D497C7FDH, 03C8295E15B9A1DE8H
  DQ 03FF5AB07DD485429H, 03C96324C054647ADH
  DQ 03FF5C9268A5946B7H, 03C3C4B1B816986A2H
  DQ 03FF5E76F15AD2148H, 03C9BA6F93080E65EH
  DQ 03FF605E1B976DC09H, 0BC93E2429B56DE47H
  DQ 03FF6247EB03A5585H, 0BC9383C17E40B497H
  DQ 03FF6434634CCC320H, 0BC8C483C759D8933H
  DQ 03FF6623882552225H, 0BC9BB60987591C34H
  DQ 03FF68155D44CA973H, 03C6038AE44F73E65H
  DQ 03FF6A09E667F3BCDH, 0BC9BDD3413B26456H
  DQ 03FF6C012750BDABFH, 0BC72895667FF0B0DH
  DQ 03FF6DFB23C651A2FH, 0BC6BBE3A683C88ABH
  DQ 03FF6FF7DF9519484H, 0BC883C0F25860EF6H
  DQ 03FF71F75E8EC5F74H, 0BC816E4786887A99H
  DQ 03FF73F9A48A58174H, 0BC90A8D96C65D53CH
  DQ 03FF75FEB564267C9H, 0BC90245957316DD3H
  DQ 03FF780694FDE5D3FH, 03C9866B80A02162DH
  DQ 03FF7A11473EB0187H, 0BC841577EE04992FH
  DQ 03FF7C1ED0130C132H, 03C9F124CD1164DD6H
  DQ 03FF7E2F336CF4E62H, 03C705D02BA15797EH
  DQ 03FF80427543E1A12H, 0BC927C86626D972BH
  DQ 03FF82589994CCE13H, 0BC9D4C1DD41532D8H
  DQ 03FF8471A4623C7ADH, 0BC88D684A341CDFBH
  DQ 03FF868D99B4492EDH, 0BC9FC6F89BD4F6BAH
  DQ 03FF88AC7D98A6699H, 03C9994C2F37CB53AH
  DQ 03FF8ACE5422AA0DBH, 03C96E9F156864B27H
  DQ 03FF8CF3216B5448CH, 0BC70D55E32E9E3AAH
  DQ 03FF8F1AE99157736H, 03C85CC13A2E3976CH
  DQ 03FF9145B0B91FFC6H, 0BC9DD6792E582524H
  DQ 03FF93737B0CDC5E5H, 0BC675FC781B57EBCH
  DQ 03FF95A44CBC8520FH, 0BC764B7C96A5F039H
  DQ 03FF97D829FDE4E50H, 0BC9D185B7C1B85D1H
  DQ 03FF9A0F170CA07BAH, 0BC9173BD91CEE632H
  DQ 03FF9C49182A3F090H, 03C7C7C46B071F2BEH
  DQ 03FF9E86319E32323H, 03C7824CA78E64C6EH
  DQ 03FFA0C667B5DE565H, 0BC9359495D1CD533H
  DQ 03FFA309BEC4A2D33H, 03C96305C7DDC36ABH
  DQ 03FFA5503B23E255DH, 0BC9D2F6EDB8D41E1H
  DQ 03FFA799E1330B358H, 03C9BCB7ECAC563C7H
  DQ 03FFA9E6B5579FDBFH, 03C90FAC90EF7FD31H
  DQ 03FFAC36BBFD3F37AH, 0BC8F9234CAE76CD0H
  DQ 03FFAE89F995AD3ADH, 03C97A1CD345DCC81H
  DQ 03FFB0E07298DB666H, 0BC9BDEF54C80E425H
  DQ 03FFB33A2B84F15FBH, 0BC62805E3084D708H
  DQ 03FFB59728DE5593AH, 0BC9C71DFBBBA6DE3H
  DQ 03FFB7F76F2FB5E47H, 0BC75584F7E54AC3BH
  DQ 03FFBA5B030A1064AH, 0BC9EFCD30E54292EH
  DQ 03FFBCC1E904BC1D2H, 03C823DD07A2D9E84H
  DQ 03FFBF2C25BD71E09H, 0BC9EFDCA3F6B9C73H
  DQ 03FFC199BDD85529CH, 03C811065895048DDH
  DQ 03FFC40AB5FFFD07AH, 03C9B4537E083C60AH
  DQ 03FFC67F12E57D14BH, 03C92884DFF483CADH
  DQ 03FFC8F6D9406E7B5H, 03C71ACBC48805C44H
  DQ 03FFCB720DCEF9069H, 03C7503CBD1E949DBH
  DQ 03FFCDF0B555DC3FAH, 0BC8DD83B53829D72H
  DQ 03FFD072D4A07897CH, 0BC9CBC3743797A9CH
  DQ 03FFD2F87080D89F2H, 0BC9D487B719D8578H
  DQ 03FFD5818DCFBA487H, 03C82ED02D75B3707H
  DQ 03FFD80E316C98398H, 0BC911EC18BEDDFE8H
  DQ 03FFDA9E603DB3285H, 03C9C2300696DB532H
  DQ 03FFDD321F301B460H, 03C92DA5778F018C3H
  DQ 03FFDFC97337B9B5FH, 0BC91A5CD4F184B5CH
  DQ 03FFE264614F5A129H, 0BC97B627817A1496H
  DQ 03FFE502EE78B3FF6H, 03C839E8980A9CC8FH
  DQ 03FFE7A51FBC74C83H, 03C92D522CA0C8DE2H
  DQ 03FFEA4AFA2A490DAH, 0BC9E9C23179C2893H
  DQ 03FFECF482D8E67F1H, 0BC9C93F3B411AD8CH
  DQ 03FFEFA1BEE615A27H, 03C9DC7F486A4B6B0H
  DQ 03FFF252B376BBA97H, 03C93A1A5BF0D8E43H
  DQ 03FFF50765B6E4540H, 03C99D3E12DD8A18BH
  DQ 03FFF7BFDAD9CBE14H, 0BC9DBB12D006350AH
  DQ 03FFFA7C1819E90D8H, 03C874853F3A5931EH
  DQ 03FFFD3C22B8F71F1H, 03C62EB74966579E7H
logLn2Hi DQ 03FE62E42FEFA3800H
logLn2Lo DQ 03D2EF35793C76730H
logP2 DQ 0BFE0000000000000H
logP3 DQ 03FD5555555555555H
logP4 DQ 0BFD0000000000000H
logP5 DQ 03FC999999999999AH
logP6 DQ 0BFC5555555555555H
logP7 DQ 03FC2492492492492H
logP8 DQ 0BFC0000000000000H
logP9 DQ 03FBC71C71C71C71CH
logTable DQ 03FF6A00000000000H, 0BFD62C82F2B9C795H, 0BC67B7AF915300E5H, 0
  DQ 03FF6800000000000H, 0BFD5D1BDBF5809CAH, 0BC74236383DC7FE1H, 0
  DQ 03FF6600000000000H, 0BFD5767717455A6CH, 0BC7526ADB283660CH, 0
  DQ 03FF6400000000000H, 0BFD51AAD872DF82DH, 0BC43927AC19F55E3H, 0
  DQ 03FF6200000000000H, 0BFD4BE5F957778A1H, 03C6259B35B04813DH, 0
  DQ 03FF6000000000000H, 0BFD4618BC21C5EC2H, 0BC7F42DECDECCF1DH, 0
  DQ 03FF5E00000000000H, 0BFD404308686A7E4H, 03C70BCFB6082CE6DH, 0
  DQ 03FF5C00000000000H, 0BFD3A64C556945EAH, 03C6C68651945F97CH, 0
  DQ 03FF5A00000000000H, 0BFD347DD9A987D55H, 03C64DD4C580919F8H, 0
  DQ 03FF5800000000000H, 0BFD2E8E2BAE11D31H, 03C78F4CDB95EBDF9H, 0
  DQ 03FF5600000000000H, 0BFD2895A13DE86A3H, 0BC77AD24C13F040EH, 0
  DQ 03FF5400000000000H, 0BFD22941FBCF7966H, 03C776F5EB09628AFH, 0
  DQ 03FF5200000000000H, 0BFD1C898C16999FBH, 03C30E5C62AFF1C44H, 0
  DQ 03FF5000000000000H, 0BFD1675CABABA60EH, 0BC2CE63EAB883717H, 0
  DQ 03FF5000000000000H, 0BFD1675CABABA60EH, 0BC2CE63EAB883717H, 0
  DQ 03FF4E00000000000H, 0BFD1058BF9AE4AD5H, 0BC589FA0AB4CB31DH, 0
  DQ 03FF4C00000000000H, 0BFD0A324E27390E3H, 0BC77DCFDE8061C03H, 0
  DQ 03FF4A00000000000H, 0BFD0402594B4D041H, 03C628EC217A5022DH, 0
  DQ 03FF4800000000000H, 0BFCFB9186D5E3E2BH, 03C6CAAAE64F21ACBH, 0
  DQ 03FF4600000000000H, 0BFCEF0ADCBDC5936H, 0BC648637950DC20DH, 0
  DQ 03FF4600000000000H, 0BFCEF0ADCBDC5936H, 0BC648637950DC20DH, 0
  DQ 03FF4400000000000H, 0BFCE27076E2AF2E6H, 03C461578001E0162H, 0
  DQ 03FF4200000000000H, 0BFCD5C216B4FBB91H, 0BC66E443597E4D40H, 0
  DQ 03FF4000000000000H, 0BFCC8FF7C79A9A22H, 03C64F689F8434012H, 0
  DQ 03FF3E00000000000H, 0BFCBC286742D8CD6H, 0BC54FCE744870F55H, 0
  DQ 03FF3E00000000000H, 0BFCBC286742D8CD6H, 0BC54FCE744870F55H, 0
  DQ 03FF3C00000000000H, 0BFCAF3C94E80BFF3H, 03C5398CFF3641985H, 0
  DQ 03FF3A00000000000H, 0BFCA23BC1FE2B563H, 0BC493711B07A998CH, 0
  DQ 03FF3800000000000H, 0BFC9525A9CF456B4H, 0BC6D904C1D4E2E26H, 0
  DQ 03FF3800000000000H, 0BFC9525A9CF456B4H, 0BC6D904C1D4E2E26H, 0
  DQ 03FF3600000000000H, 0BFC87FA06520C911H, 03C6BF7FDBFA08D9AH, 0
  DQ 03FF3400000000000H, 0BFC7AB890210D909H, 0BC4BE36B2D6A0608H, 0
  DQ 03FF3200000000000H, 0BFC6D60FE719D21DH, 03C6CAAE268ECD179H, 0
  DQ 03FF3200000000000H, 0BFC6D60FE719D21DH, 03C6CAAE268ECD179H, 0
  DQ 03FF3000000000000H, 0BFC5FF3070A793D4H, 03C5BC60EFAFC6F6EH, 0
  DQ 03FF2E00000000000H, 0BFC526E5E3A1B438H, 03C6746FF8A470D3AH, 0
  DQ 03FF2E00000000000H, 0BFC526E5E3A1B438H, 03C6746FF8A470D3AH, 0
  DQ 03FF2C00000000000H, 0BFC44D2B6CCB7D1EH, 0BC69F4F6543E1F88H, 0
  DQ 03FF2A00000000000H, 0BFC371FC201E8F74H, 0BC5DE6CB62AF18A0H, 0
  DQ 03FF2A00000000000H, 0BFC371FC201E8F74H, 0BC5DE6CB62AF18A0H, 0
  DQ 03FF2800000000000H, 0BFC29552F81FF523H, 0BC6301771C407DBFH, 0
  DQ 03FF2600000000000H, 0BFC1B72AD52F67A0H, 0BC5483023472CD74H, 0
  DQ 03FF2600000000000H, 0BFC1B72AD52F67A0H, 0BC5483023472CD74H, 0
  DQ 03FF2400000000000H, 0BFC0D77E7CD08E59H, 0BC69A5DC5E9030ACH, 0
  DQ 03FF2200000000000H, 0BFBFEC9131DBEABBH, 03C55746B9981B36CH, 0
  DQ 03FF2200000000000H, 0BFBFEC9131DBEABBH, 03C55746B9981B36CH, 0
  DQ 03FF2000000000000H, 0BFBE27076E2AF2E6H, 03C361578001E0162H, 0
  DQ 03FF1E00000000000H, 0BFBC5E548F5BC743H, 0BC35D617EF8161B1H, 0
  DQ 03FF1E00000000000H, 0BFBC5E548F5BC743H, 0BC35D617EF8161B1H, 0
  DQ 03FF1C00000000000H, 0BFBA926D3A4AD563H, 0BC5942F48AA70EA9H, 0
  DQ 03FF1C00000000000H, 0BFBA926D3A4AD563H, 0BC5942F48AA70EA9H, 0
  DQ 03FF1A00000000000H, 0BFB8C345D6319B21H, 03C24A697AB3424A9H, 0
  DQ 03FF1800000000000H, 0BFB6F0D28AE56B4CH, 03C5906D99184B992H, 0
  DQ 03FF1800000000000H, 0BFB6F0D28AE56B4CH, 03C5906D99184B992H, 0
  DQ 03FF1600000000000H, 0BFB51B073F06183FH, 0BC5A49E39A1A8BE4H, 0
  DQ 03FF1600000000000H, 0BFB51B073F06183FH, 0BC5A49E39A1A8BE4H, 0
  DQ 03FF1400000000000H, 0BFB341D7961BD1D1H, 03C5B599F227BECBBH, 0
  DQ 03FF1200000000000H, 0BFB16536EEA37AE1H, 03C379DA3E8C22CDAH, 0
  DQ 03FF1200000000000H, 0BFB16536EEA37AE1H, 03C379DA3E8C22CDAH, 0
  DQ 03FF1000000000000H, 0BFAF0A30C01162A6H, 0BC485F325C5BBACDH, 0
  DQ 03FF1000000000000H, 0BFAF0A30C01162A6H, 0BC485F325C5BBACDH, 0
  DQ 03FF0E00000000000H, 0BFAB42DD711971BFH, 03C3EB9759C130499H, 0
  DQ 03FF0E00000000000H, 0BFAB42DD711971BFH, 03C3EB9759C130499H, 0
  DQ 03FF0C00000000000H, 0BFA77458F632DCFCH, 0BC418D3CA87B9296H, 0
  DQ 03FF0A00000000000H, 0BFA39E87B9FEBD60H, 03C45BFA937F551BBH, 0
  DQ 03FF0A00000000000H, 0BFA39E87B9FEBD60H, 03C45BFA937F551BBH, 0
  DQ 03FF0800000000000H, 0BF9F829B0E783300H, 0BC333E3F04F1EF23H, 0
  DQ 03FF0800000000000H, 0BF9F829B0E783300H, 0BC333E3F04F1EF23H, 0
  DQ 03FF0600000000000H, 0BF97B91B07D5B11BH, 03C35B602ACE3A510H, 0
  DQ 03FF0600000000000H, 0BF97B91B07D5B11BH, 03C35B602ACE3A510H, 0
  DQ 03FF0400000000000H, 0BF8FC0A8B0FC03E4H, 03C183092C59642A1H, 0
  DQ 03FF0400000000000H, 0BF8FC0A8B0FC03E4H, 03C183092C59642A1H, 0
  DQ 03FF0200000000000H, 0BF7FE02A6B106789H, 03BCE44B7E3711EBFH, 0
  DQ 03FF0200000000000H, 0BF7FE02A6B106789H, 03BCE44B7E3711EBFH, 0
  DQ 03FF0000000000000H, 00000000000000000H, 00000000000000000H, 0
  DQ 03FEFE00000000000H, 03F70080559588B35H, 03C1F96638CF63677H, 0
  DQ 03FEFA00000000000H, 03F882448A388A2AAH, 03C104B16137F09A0H, 0
  DQ 03FEF600000000000H, 03F9432A925980CC1H, 0BC38CDAF39004192H, 0
  DQ 03FEF200000000000H, 03F9C63D2EC14AAF2H, 0BC3CE030A686BD86H, 0
  DQ 03FEEE00000000000H, 03FA252F32F8D183FH, 0BC4947F792615916H, 0
  DQ 03FEEA00000000000H, 03FA67C94F2D4BB58H, 03C40413E6505E603H, 0
  DQ 03FEE800000000000H, 03FA894AA149FB343H, 03C3A8BE97660A23DH, 0
  DQ 03FEE400000000000H, 03FACCB73CDDDB2CCH, 0BC4E48FB0500EFD4H, 0
  DQ 03FEE000000000000H, 03FB08598B59E3A07H, 0BC5DD7009902BF32H, 0
  DQ 03FEDC00000000000H, 03FB2AA04A44717A5H, 0BC5D15D38D2FA3F7H, 0
  DQ 03FEDA00000000000H, 03FB3BDF5A7D1EE64H, 03C47A976D3B5B45FH, 0
  DQ 03FED600000000000H, 03FB5E95A4D9791CBH, 03C5F38745C5C450AH, 0
  DQ 03FED200000000000H, 03FB8197E2F40E3F0H, 03C3B9F2DFFBEED43H, 0
  DQ 03FED000000000000H, 03FB9335E5D594989H, 0BC5478A85704CCB7H, 0
  DQ 03FECC00000000000H, 03FBB6AC88DAD5B1CH, 0BC40057EED1CA59FH, 0
  DQ 03FEC800000000000H, 03FBDA727638446A2H, 03C5401FA71733019H, 0
  DQ 03FEC600000000000H, 03FBEC739830A1120H, 0BC4A2BF991780D3FH, 0
  DQ 03FEC200000000000H, 03FC08598B59E3A07H, 0BC6DD7009902BF32H, 0
  DQ 03FEC000000000000H, 03FC1178E8227E47CH, 0BC50E63A5F01C691H, 0
  DQ 03FEBC00000000000H, 03FC23D712A49C202H, 0BC66E38161051D69H, 0
  DQ 03FEBA00000000000H, 03FC2D1610C86813AH, 0BC5499A3F25AF95FH, 0
  DQ 03FEB600000000000H, 03FC3FB45A59928CCH, 0BC6D87E6A354D056H, 0
  DQ 03FEB400000000000H, 03FC4913D8333B561H, 0BC50D5604930F135H, 0
  DQ 03FEB000000000000H, 03FC5BF406B543DB2H, 0BC21F5B44C0DF7E7H, 0
  DQ 03FEAE00000000000H, 03FC6574EBE8C133AH, 0BC3D34F0F4621BEDH, 0
  DQ 03FEAA00000000000H, 03FC7898D85444C73H, 03C5EF8F6EBCFB201H, 0
  DQ 03FEA800000000000H, 03FC823C16551A3C2H, 0BC61232CE70BE781H, 0
  DQ 03FEA600000000000H, 03FC8BEAFEB38FE8CH, 03C555AA8B6997A40H, 0
  DQ 03FEA200000000000H, 03FC9F6C407089664H, 03C435A19605E67EFH, 0
  DQ 03FEA000000000000H, 03FCA93ED3C8AD9E3H, 03C6BCAFA9DE97203H, 0
  DQ 03FE9E00000000000H, 03FCB31D8575BCE3DH, 0BC66353AB386A94DH, 0
  DQ 03FE9A00000000000H, 03FCC6FFBC6F00F71H, 0BC68E58B2C57A4A5H, 0
  DQ 03FE9800000000000H, 03FCD1037F2655E7BH, 03C660629242471A2H, 0
  DQ 03FE9600000000000H, 03FCDB13DB0D48940H, 03C5AA11D49F96CB9H, 0
  DQ 03FE9400000000000H, 03FCE530EFFE71012H, 03C42276041F43042H, 0
  DQ 03FE9000000000000H, 03FCF991C6CB3B379H, 03C6F665066F980A2H, 0
  DQ 03FE8E00000000000H, 03FD01EAE5626C691H, 0BC418290BD2932E2H, 0
  DQ 03FE8C00000000000H, 03FD07138604D5862H, 03C7CDB16ED4E9138H, 0
  DQ 03FE8A00000000000H, 03FD0C42D676162E3H, 03C5162C79D5D11EEH, 0
  DQ 03FE8800000000000H, 03FD1178E8227E47CH, 0BC60E63A5F01C691H, 0
  DQ 03FE8400000000000H, 03FD1BF99635A6B95H, 0BC612AEB84249223H, 0
  DQ 03FE8200000000000H, 03FD214456D0EB8D4H, 03C6F7AE91AEBA60AH, 0
  DQ 03FE8000000000000H, 03FD269621134DB92H, 03C7E0EFADD9DB02BH, 0
  DQ 03FE7E00000000000H, 03FD2BEF07CDC9354H, 0BC782DAD7FD86088H, 0
  DQ 03FE7C00000000000H, 03FD314F1E1D35CE4H, 0BC73D69909E5C3DCH, 0
  DQ 03FE7A00000000000H, 03FD36B6776BE1117H, 0BC5324F0E883858EH, 0
  DQ 03FE7800000000000H, 03FD3C25277333184H, 0BC72AD27E50A8EC6H, 0
  DQ 03FE7600000000000H, 03FD419B423D5E8C7H, 03C60DBB243827392H, 0
  DQ 03FE7400000000000H, 03FD4718DC271C41BH, 03C38FB4C14C56EEFH, 0
  DQ 03FE7200000000000H, 03FD4C9E09E172C3CH, 0BC5123615B147A5DH, 0
  DQ 03FE7000000000000H, 03FD522AE0738A3D8H, 0BC68F7E9B38A6979H, 0
  DQ 03FE6E00000000000H, 03FD57BF753C8D1FBH, 0BC60908D15F88B63H, 0
  DQ 03FE6C00000000000H, 03FD5D5BDDF595F30H, 0BC76541148CBB8A2H, 0
trigPio2a DQ 03FF921FB54400000H
trigPio2b DQ 03DD0B4611A600000H
trigPio2c DQ 03BA3198A2E037073H
trigTwoOverPi DQ 03FE45F306DC9C883H
sinS3 DQ 0BFC5555555555555H
cosC4 DQ 03FA5555555555555H
sinS5 DQ 03F81111111111111H
cosC6 DQ 0BF56C16C16C16C17H
sinS7 DQ 0BF2A01A01A01A01AH
cosC8 DQ 03EFA01A01A01A01AH
sinS9 DQ 03EC71DE3A556C734H
cosC10 DQ 0BE927E4FB7789F5CH
sinS11 DQ 0BE5AE64567F544E4H
cosC12 DQ 03E21EED8EFF8D898H
sinS13 DQ 03DE6124613A86D09H
cosC14 DQ 0BDA93974A8C07C9DH
sinS15 DQ 0BD6AE7F3E733B81FH
cosC16 DQ 03D2AE7F3E733B81FH
sinS17 DQ 03CE952C77030AD4AH
cosC18 DQ 0BCA6827863B97D97H
one DQ 03FF0000000000000H
half DQ 03FE0000000000000H
shifter DQ 04338000000000000H
splitter DQ 041A0000002000000H
_DATA ENDS

_TEXT SEGMENT

exp PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3C90000000000000h
    sub rcx, rdx
    mov rdx, 03F6200000000000h
    cmp rcx, rdx
    jae expSlow
    cmp DWORD PTR __grt_fma, 0
    jne expFma
    movsd xmm1, QWORD PTR expScale
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR shifter
    addsd xmm1, xmm2
    movq rax, xmm1
    subsd xmm1, xmm2
    movsd xmm3, QWORD PTR expStepHi
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
    movsd xmm3, QWORD PTR expStepLo
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC4
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC3
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC2
    mulsd xmm2, xmm1
    addsd xmm2, xmm0
    mov ecx, eax
    and ecx, 127
    sar eax, 7
    shl rcx, 4
    lea rdx, expTable
    movsd xmm3, QWORD PTR [rdx+rcx]
    movsd xmm4, QWORD PTR [rdx+rcx+8]
    mulsd xmm2, xmm3
    addsd xmm2, xmm4
    addsd xmm2, xmm3
    movsxd rax, eax
    shl rax, 52
    movq rcx, xmm2
    add rcx, rax
    movq xmm0, rcx
    ret
expFma:
    movsd xmm1, QWORD PTR expScale
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR shifter
    addsd xmm1, xmm2
    movq rax, xmm1
    subsd xmm1, xmm2
    movsd xmm3, QWORD PTR expStepHi
    DB 0C4h, 0E2h, 0F1h, 0BDh, 0C3h    ; vfnmadd231sd xmm0, xmm1, xmm3
    movsd xmm3, QWORD PTR expStepLo
    DB 0C4h, 0E2h, 0F1h, 0BDh, 0C3h    ; vfnmadd231sd xmm0, xmm1, xmm3
    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    movsd xmm3, QWORD PTR expC4
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC3
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC2
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    DB 0C4h, 0E2h, 0F1h, 0A9h, 0D0h    ; vfmadd213sd xmm2, xmm1, xmm0
    mov ecx, eax
    and ecx, 127
    sar eax, 7
    shl rcx, 4
    lea rdx, expTable
    movsd xmm3, QWORD PTR [rdx+rcx]
    movsd xmm4, QWORD PTR [rdx+rcx+8]
    DB 0C4h, 0E2h, 0E1h, 0A9h, 0D4h    ; vfmadd213sd xmm2, xmm3, xmm4
    addsd xmm2, xmm3
    movsxd rax, eax
    shl rax, 52
    movq rcx, xmm2
    add rcx, rax
    movq xmm0, rcx
    ret
expSlow:
    jmp __grt_exp_slow
exp ENDP

log PROC
    movq rax, xmm0
    mov rcx, rax
    mov rdx, 0010000000000000h
    sub rcx, rdx
    mov rdx, 7FE0000000000000h
    cmp rcx, rdx
    jae logSlow
    cmp DWORD PTR __grt_fma, 0
    jne logFma
    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    and rax, -256
    movq xmm1, rax
    subsd xmm0, xmm1
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    mulsd xmm1, xmm2
    subsd xmm1, QWORD PTR one
    mulsd xmm0, xmm2
    movapd xmm3, xmm1
    addsd xmm3, xmm0
    movapd xmm4, xmm3
    mulsd xmm4, xmm3
    movsd xmm5, QWORD PTR logP8
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP6
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP4
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP2
    movsd xmm2, QWORD PTR logP7
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP5
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP3
    mulsd xmm2, xmm3
    addsd xmm5, xmm2
    mulsd xmm5, xmm4
    addsd xmm5, xmm0
    xorps xmm2, xmm2
    cvtsi2sd xmm2, r8
    movsd xmm3, QWORD PTR logLn2Hi
    mulsd xmm3, xmm2
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm0, xmm3
    addsd xmm0, xmm4
    subsd xmm3, xmm0
    addsd xmm3, xmm4
    addsd xmm5, xmm3
    movapd xmm4, xmm0
    addsd xmm4, xmm1
    subsd xmm0, xmm4
    addsd xmm0, xmm1
    addsd xmm5, xmm0
    mulsd xmm2, QWORD PTR logLn2Lo
    addsd xmm2, QWORD PTR [rdx+r9+16]
    addsd xmm5, xmm2
    addsd xmm4, xmm5
    movapd xmm0, xmm4
    ret
logFma:
    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    movsd xmm1, QWORD PTR one
    DB 0C4h, 0E2h, 0E9h, 0ABh, 0C1h    ; vfmsub213sd xmm0, xmm2, xmm1
    movapd xmm4, xmm0
    mulsd xmm4, xmm0
    movsd xmm5, QWORD PTR logP8
    movsd xmm1, QWORD PTR logP6
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP4
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP2
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm2, QWORD PTR logP7
    movsd xmm1, QWORD PTR logP5
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0D1h    ; vfmadd213sd xmm2, xmm4, xmm1
    movsd xmm1, QWORD PTR logP3
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0D1h    ; vfmadd213sd xmm2, xmm4, xmm1
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D5h    ; vfmadd213sd xmm2, xmm0, xmm5
    mulsd xmm2, xmm4
    xorps xmm3, xmm3
    cvtsi2sd xmm3, r8
    movsd xmm1, QWORD PTR logLn2Hi
    mulsd xmm1, xmm3
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm5, xmm1
    addsd xmm5, xmm4
    subsd xmm1, xmm5
    addsd xmm1, xmm4
    addsd xmm2, xmm1
    movapd xmm4, xmm5
    addsd xmm4, xmm0
    subsd xmm5, xmm4
    addsd xmm5, xmm0
    addsd xmm2, xmm5
    mulsd xmm3, QWORD PTR logLn2Lo
    addsd xmm3, QWORD PTR [rdx+r9+16]
    addsd xmm2, xmm3
    movapd xmm5, xmm2
    addsd xmm4, xmm5
    movapd xmm0, xmm4
    ret
logSlow:
    jmp __grt_log_slow
log ENDP

sin PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3E50000000000000h
    cmp rcx, rdx
    jb sinTiny
    mov rdx, 412E848000000000h
    cmp rcx, rdx
    jae sinSlow
    xor r8d, r8d
    jmp trigReduce
sinTiny:
    ret
sinSlow:
    jmp __grt_sin_slow
sin ENDP

cos PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3E40000000000000h
    cmp rcx, rdx
    jb cosTiny
    mov rdx, 412E848000000000h
    cmp rcx, rdx
    jae cosSlow
    mov r8d, 1
    jmp trigReduce
cosTiny:
    movsd xmm0, QWORD PTR one
    ret
cosSlow:
    jmp __grt_cos_slow
cos ENDP

trigReduce PROC
    movapd xmm1, xmm0
    mulsd xmm1, QWORD PTR trigTwoOverPi
    addsd xmm1, QWORD PTR shifter
    movq rax, xmm1
    add eax, r8d
    subsd xmm1, QWORD PTR shifter
    movapd xmm2, xmm1
    mulsd xmm2, QWORD PTR trigPio2a
    subsd xmm0, xmm2
    movapd xmm3, xmm1
    mulsd xmm3, QWORD PTR trigPio2b
    movapd xmm2, xmm0
    subsd xmm2, xmm3
    subsd xmm0, xmm2
    subsd xmm0, xmm3
    mulsd xmm1, QWORD PTR trigPio2c
    subsd xmm0, xmm1
    movapd xmm1, xmm2
    addsd xmm1, xmm0
    subsd xmm2, xmm1
    addsd xmm2, xmm0
    movapd xmm3, xmm1
    mulsd xmm3, xmm1
    movapd xmm5, xmm3
    mulsd xmm5, xmm3
    cmp DWORD PTR __grt_fma, 0
    jne trigFma
    test eax, 1
    jnz trigCosine
    movsd xmm4, QWORD PTR sinS15
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS11
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS7
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS3
    movsd xmm0, QWORD PTR sinS17
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS13
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS9
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS5
    mulsd xmm0, xmm3
    addsd xmm4, xmm0
    mulsd xmm4, xmm3
    mulsd xmm4, xmm1
    addsd xmm4, xmm2
    addsd xmm1, xmm4
    movapd xmm0, xmm1
    jmp trigSign
trigCosine:
    movsd xmm4, QWORD PTR cosC16
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC12
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC8
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC4
    movsd xmm0, QWORD PTR cosC18
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC14
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC10
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC6
    mulsd xmm0, xmm3
    addsd xmm4, xmm0
    mulsd xmm4, xmm5
    mulsd xmm2, xmm1
    subsd xmm4, xmm2
    movapd xmm5, xmm3
    mulsd xmm5, QWORD PTR half
    movsd xmm0, QWORD PTR one
    subsd xmm0, xmm5
    movsd xmm2, QWORD PTR one
    subsd xmm2, xmm0
    subsd xmm2, xmm5
    addsd xmm2, xmm4
    addsd xmm0, xmm2
    jmp trigSign
trigFma:
    test eax, 1
    jnz trigFmaCosine
    movsd xmm4, QWORD PTR sinS17
    movsd xmm0, QWORD PTR sinS13
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS9
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS5
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movq rcx, xmm4
    movsd xmm4, QWORD PTR sinS15
    movsd xmm0, QWORD PTR sinS11
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS7
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS3
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movq xmm0, rcx
    DB 0C4h, 0E2h, 0F9h, 0B9h, 0E3h    ; vfmadd231sd xmm4, xmm0, xmm3
    mulsd xmm4, xmm3
    DB 0C4h, 0E2h, 0F1h, 0A9h, 0E2h    ; vfmadd213sd xmm4, xmm1, xmm2
    addsd xmm1, xmm4
    movapd xmm0, xmm1
    jmp trigSign
trigFmaCosine:
    movsd xmm4, QWORD PTR cosC18
    movsd xmm0, QWORD PTR cosC14
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC10
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC6
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movq rcx, xmm4
    movsd xmm4, QWORD PTR cosC16
    movsd xmm0, QWORD PTR cosC12
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC8
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC4
    DB 0C4h, 0E2h, 0D1h, 0A9h, 0E0h    ; vfmadd213sd xmm4, xmm5, xmm0
    movq xmm0, rcx
    DB 0C4h, 0E2h, 0F9h, 0B9h, 0E3h    ; vfmadd231sd xmm4, xmm0, xmm3
    mulsd xmm4, xmm5
    mulsd xmm2, xmm1
    subsd xmm4, xmm2
    movapd xmm5, xmm3
    mulsd xmm5, QWORD PTR half
    movsd xmm0, QWORD PTR one
    subsd xmm0, xmm5
    movsd xmm2, QWORD PTR one
    subsd xmm2, xmm0
    subsd xmm2, xmm5
    addsd xmm2, xmm4
    addsd xmm0, xmm2
trigSign:
    test eax, 2
    jz trigDone
    movq rcx, xmm0
    btc rcx, 63
    movq xmm0, rcx
trigDone:
    ret
trigReduce ENDP

pow PROC
    movq r10, xmm0
    movq r11, xmm1
    mov rcx, r10
    mov rdx, 0010000000000000h
    sub rcx, rdx
    mov rdx, 7FE0000000000000h
    cmp rcx, rdx
    jae powSlow
    mov rcx, r11
    btr rcx, 63
    mov rdx, 3C30000000000000h
    sub rcx, rdx
    mov rdx, 0780000000000000h
    cmp rcx, rdx
    jae powSlow
    mov rax, r10
    cmp DWORD PTR __grt_fma, 0
    jne powFma
    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    and rax, -256
    movq xmm1, rax
    subsd xmm0, xmm1
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    mulsd xmm1, xmm2
    subsd xmm1, QWORD PTR one
    mulsd xmm0, xmm2
    movapd xmm3, xmm1
    addsd xmm3, xmm0
    movapd xmm4, xmm3
    mulsd xmm4, xmm3
    movsd xmm5, QWORD PTR logP8
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP6
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP4
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP2
    movsd xmm2, QWORD PTR logP9
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP7
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP5
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP3
    mulsd xmm2, xmm3
    addsd xmm5, xmm2
    mulsd xmm5, xmm4
    addsd xmm5, xmm0
    xorps xmm2, xmm2
    cvtsi2sd xmm2, r8
    movsd xmm3, QWORD PTR logLn2Hi
    mulsd xmm3, xmm2
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm0, xmm3
    addsd xmm0, xmm4
    subsd xmm3, xmm0
    addsd xmm3, xmm4
    addsd xmm5, xmm3
    movapd xmm4, xmm0
    addsd xmm4, xmm1
    subsd xmm0, xmm4
    addsd xmm0, xmm1
    addsd xmm5, xmm0
    mulsd xmm2, QWORD PTR logLn2Lo
    addsd xmm2, QWORD PTR [rdx+r9+16]
    addsd xmm5, xmm2
    movapd xmm0, xmm4
    addsd xmm0, xmm5
    subsd xmm4, xmm0
    addsd xmm4, xmm5
    movq r9, xmm4
    movq xmm1, r11
    movapd xmm2, xmm1
    mulsd xmm2, xmm0
    movapd xmm3, xmm1
    mulsd xmm3, QWORD PTR splitter
    movapd xmm5, xmm3
    subsd xmm5, xmm1
    subsd xmm3, xmm5
    subsd xmm1, xmm3
    movapd xmm5, xmm0
    mulsd xmm5, QWORD PTR splitter
    movapd xmm4, xmm5
    subsd xmm4, xmm0
    subsd xmm5, xmm4
    subsd xmm0, xmm5
    movapd xmm4, xmm3
    mulsd xmm4, xmm5
    subsd xmm4, xmm2
    mulsd xmm3, xmm0
    addsd xmm4, xmm3
    mulsd xmm5, xmm1
    addsd xmm4, xmm5
    mulsd xmm1, xmm0
    addsd xmm4, xmm1
    movq xmm1, r11
    movq xmm0, r9
    mulsd xmm1, xmm0
    addsd xmm4, xmm1
    jmp powExp
powFma:
    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    movsd xmm1, QWORD PTR one
    DB 0C4h, 0E2h, 0E9h, 0ABh, 0C1h    ; vfmsub213sd xmm0, xmm2, xmm1
    movapd xmm4, xmm0
    mulsd xmm4, xmm0
    movsd xmm5, QWORD PTR logP8
    movsd xmm1, QWORD PTR logP6
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP4
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP2
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0E9h    ; vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm2, QWORD PTR logP9
    movsd xmm1, QWORD PTR logP7
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0D1h    ; vfmadd213sd xmm2, xmm4, xmm1
    movsd xmm1, QWORD PTR logP5
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0D1h    ; vfmadd213sd xmm2, xmm4, xmm1
    movsd xmm1, QWORD PTR logP3
    DB 0C4h, 0E2h, 0D9h, 0A9h, 0D1h    ; vfmadd213sd xmm2, xmm4, xmm1
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D5h    ; vfmadd213sd xmm2, xmm0, xmm5
    mulsd xmm2, xmm4
    xorps xmm3, xmm3
    cvtsi2sd xmm3, r8
    movsd xmm1, QWORD PTR logLn2Hi
    mulsd xmm1, xmm3
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm5, xmm1
    addsd xmm5, xmm4
    subsd xmm1, xmm5
    addsd xmm1, xmm4
    addsd xmm2, xmm1
    movapd xmm4, xmm5
    addsd xmm4, xmm0
    subsd xmm5, xmm4
    addsd xmm5, xmm0
    addsd xmm2, xmm5
    mulsd xmm3, QWORD PTR logLn2Lo
    addsd xmm3, QWORD PTR [rdx+r9+16]
    addsd xmm2, xmm3
    movapd xmm5, xmm2
    movapd xmm0, xmm4
    addsd xmm0, xmm5
    subsd xmm4, xmm0
    addsd xmm4, xmm5
    movq xmm1, r11
    movapd xmm2, xmm1
    mulsd xmm2, xmm0
    movapd xmm3, xmm1
    DB 0C4h, 0E2h, 0F9h, 0ABh, 0DAh    ; vfmsub213sd xmm3, xmm0, xmm2
    DB 0C4h, 0E2h, 0F1h, 0B9h, 0DCh    ; vfmadd231sd xmm3, xmm1, xmm4
    movapd xmm4, xmm3
powExp:
    movq rax, xmm2
    btr rax, 63
    mov rdx, 4086200000000000h
    cmp rax, rdx
    jae powSlow
    movapd xmm0, xmm2
    cmp DWORD PTR __grt_fma, 0
    jne powExpFma
    movsd xmm1, QWORD PTR expScale
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR shifter
    addsd xmm1, xmm2
    movq rax, xmm1
    subsd xmm1, xmm2
    movsd xmm3, QWORD PTR expStepHi
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
    movsd xmm3, QWORD PTR expStepLo
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
    addsd xmm0, xmm4
    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC4
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC3
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC2
    mulsd xmm2, xmm1
    addsd xmm2, xmm0
    mov ecx, eax
    and ecx, 127
    sar eax, 7
    shl rcx, 4
    lea rdx, expTable
    movsd xmm3, QWORD PTR [rdx+rcx]
    movsd xmm4, QWORD PTR [rdx+rcx+8]
    mulsd xmm2, xmm3
    addsd xmm2, xmm4
    addsd xmm2, xmm3
    movsxd rax, eax
    shl rax, 52
    movq rcx, xmm2
    add rcx, rax
    movq xmm0, rcx
    ret
powExpFma:
    movsd xmm1, QWORD PTR expScale
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR shifter
    addsd xmm1, xmm2
    movq rax, xmm1
    subsd xmm1, xmm2
    movsd xmm3, QWORD PTR expStepHi
    DB 0C4h, 0E2h, 0F1h, 0BDh, 0C3h    ; vfnmadd231sd xmm0, xmm1, xmm3
    movsd xmm3, QWORD PTR expStepLo
    DB 0C4h, 0E2h, 0F1h, 0BDh, 0C3h    ; vfnmadd231sd xmm0, xmm1, xmm3
    addsd xmm0, xmm4
    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    movsd xmm3, QWORD PTR expC4
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC3
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC2
    DB 0C4h, 0E2h, 0F9h, 0A9h, 0D3h    ; vfmadd213sd xmm2, xmm0, xmm3
    DB 0C4h, 0E2h, 0F1h, 0A9h, 0D0h    ; vfmadd213sd xmm2, xmm1, xmm0
    mov ecx, eax
    and ecx, 127
    sar eax, 7
    shl rcx, 4
    lea rdx, expTable
    movsd xmm3, QWORD PTR [rdx+rcx]
    movsd xmm4, QWORD PTR [rdx+rcx+8]
    DB 0C4h, 0E2h, 0E1h, 0A9h, 0D4h    ; vfmadd213sd xmm2, xmm3, xmm4
    addsd xmm2, xmm3
    movsxd rax, eax
    shl rax, 52
    movq rcx, xmm2
    add rcx, rax
    movq xmm0, rcx
    ret
powSlow:
    movq xmm0, r10
    movq xmm1, r11
    jmp __grt_pow_slow
pow ENDP

_TEXT ENDS

END
