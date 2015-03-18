#include <xc.inc>

GLOBAL _bit_count

wait MACRO cy3
    movlw cy3
    decfsz WREG
    bra $-1
ENDM

sample_bit_and_wait MACRO
    movf PORTA, w         ; Sample PORTA. Bit 5 is the input.
    movwi FSR0++          ; Save the bit into the color array.
    wait 22
    nop
ENDM

PSECT intentry,class=CODE,reloc=2,delta=2
    BANKSEL PORTA
    decfsz _bit_count, f  ; Decrement bit count. If zero, we're in echo mode.
    bra read_mode

    ; echo mode handling.
    lsrf PORTA, f         ; Shift the input bit to the output position.
                          ; We're setting all PORTA bits, but bit 4 is the only output.

  cleanup:
    clrf TMR0             ; Clear the idle time counter.
    movlw 1               ; Increment bit count.
    addwf _bit_count
    BANKSEL IOCAF         ; Clear the interrupt.
    clrf BANKMASK(IOCAF)
    retfie

    ; read mode handling.
  read_mode:
    ; Was this a falling edge (start bit)? Otherwise, ignore it.
    btfsc PORTA, 5
    bra cleanup

    ; At this point, 6 cycles have elapsed since the beginning of the ISR
    ; + between 3-4 cycles since the edge. Total: 9-10 cycles. Our first sample
    ; takes place 1.5 serial bits from the edge, or 13[us] or 104 cycles.
    ; Let's then idle for 94 cycles (=93+1).
    wait 31
    nop

    ; Repeat 8 times: sample, then wait until a period of 1 serial bit time or
    ; about 69 cycles has elapsed.
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    sample_bit_and_wait
    ; We should be on the stop bit now.

    ; Decrement 8 from bit_count.
    movlw 8
    subwf _bit_count, f

    ; Save FSR0 to the shadow register.
    movf FSR0L, w
    BANKSEL FSR0L_SHAD
    movwf BANKMASK(FSR0L_SHAD)

    bra cleanup
