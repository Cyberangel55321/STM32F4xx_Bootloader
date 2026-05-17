    AREA    |.text|, CODE, READONLY
    THUMB
    PRESERVE8


JumpApp         PROC
								EXPORT  JumpApp
								
                LDR     SP, [R0, #0]
                LDR     PC, [R0, #4]
                ENDP

    END
