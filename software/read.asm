#include <xc.inc>

readbit MACRO dest
    LOCAL wait_rising
    LOCAL wait_falling
    wait_rising:
    btfss PORTA, 0
    bra wait_rising

    clrw
    lslf dest, f
    nop

    btfsc PORTA, 0
    movlw 1
    iorwf dest, f

    wait_falling:
    btfsc PORTA, 0
    bra wait_falling
ENDM

GLOBAL _Read
GLOBAL _r, _g, _b

PSECT text,class=CODE,reloc=2,delta=2
  _Read:
    REPT 8
    readbit _g
    ENDM

    REPT 8
    readbit _r
    ENDM

    REPT 8
    readbit _b
    ENDM

    return

  PSECT intentry,class=CODE,reloc=2,delta=2
    rlf PORTA, f
    clrf TMR0
    BANKSEL(IOCAF)
    clrf BANKMASK(IOCAF)
    retfie
